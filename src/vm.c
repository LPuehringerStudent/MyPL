#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vm.h"
#include "natives.h"
#include "sql_engine.h"

struct VM {
    Chunk*        chunk;
    uint8_t*      ip;
    Value         stack[STACK_MAX];
    Value*        stack_top;
    Value*        frames[STACK_MAX];
    uint8_t*      return_ips[STACK_MAX];
    int           frame_count;
    Value*        frame_base;
    int           local_count;
    Value         repl_locals[STACK_MAX];
    int           repl_local_count;
    void*         result_handle;
    void*         row_handle;
    struct Context* context;
    DBDriver*     driver;
    Value         sql_params[16];
    int           sql_param_count;
    int           sql_line;
    char          error_message[256];
};

VM* vm_init(void) {
    VM* vm = malloc(sizeof(VM));
    if (vm == NULL) return NULL;
    vm->chunk = NULL;
    vm->ip = NULL;
    vm->stack_top = vm->stack;
    vm->frame_count = 0;
    vm->frame_base = vm->stack;
    vm->local_count = 0;
    vm->repl_local_count = 0;
    vm->result_handle = NULL;
    vm->row_handle = NULL;
    vm->context = NULL;
    vm->driver = NULL;
    vm->sql_param_count = 0;
    vm->sql_line = 0;
    vm->error_message[0] = '\0';
    return vm;
}

static int current_line(VM* vm) {
    if (vm->chunk == NULL || vm->chunk->lines == NULL || vm->ip == NULL) return 0;
    int offset = (int)(vm->ip - vm->chunk->code) - 1;
    if (offset < 0 || offset >= vm->chunk->lines_count) return 0;
    return vm->chunk->lines[offset];
}

static void set_runtime_error(VM* vm, const char* message) {
    int line = current_line(vm);
    if (line > 0) {
        snprintf(vm->error_message, sizeof(vm->error_message), "[line %d] %s", line, message);
    } else {
        snprintf(vm->error_message, sizeof(vm->error_message), "%s", message);
    }
}

static void set_runtime_error_from_driver(VM* vm, const char* prefix) {
    if (vm->driver != NULL && vm->driver->error_message[0] != '\0') {
        char msg[512];
        snprintf(msg, sizeof(msg), "%s: %s", prefix, vm->driver->error_message);
        set_runtime_error(vm, msg);
    } else {
        set_runtime_error(vm, prefix);
    }
}

static void set_runtime_error_sql(VM* vm, const char* message) {
    if (vm->sql_line > 0) {
        char msg[512];
        snprintf(msg, sizeof(msg), "SQL error at line %d: %s", vm->sql_line, message);
        set_runtime_error(vm, msg);
    } else {
        set_runtime_error(vm, message);
    }
}

static void set_runtime_error_from_driver_sql(VM* vm, const char* prefix) {
    if (vm->driver != NULL && vm->driver->error_message[0] != '\0') {
        char msg[512];
        snprintf(msg, sizeof(msg), "%s: %s", prefix, vm->driver->error_message);
        set_runtime_error_sql(vm, msg);
    } else {
        set_runtime_error_sql(vm, prefix);
    }
}

const char* vm_get_error(VM* vm) {
    if (vm == NULL) return NULL;
    return vm->error_message[0] != '\0' ? vm->error_message : NULL;
}

void vm_set_error(VM* vm, const char* message) {
    if (vm == NULL) return;
    set_runtime_error(vm, message);
}

void vm_free(VM* vm) {
    if (vm == NULL) return;
    for (Value* p = vm->stack; p < vm->stack_top; p++) {
        value_release(*p);
    }
    for (int i = 0; i < vm->repl_local_count; i++) {
        value_release(vm->repl_locals[i]);
    }
    if (vm->driver != NULL && vm->result_handle != NULL) {
        vm->driver->result_free(vm->driver, vm->result_handle);
    } else if (vm->driver == NULL) {
        result_free((Result*)vm->result_handle);
    }
    array_pool_free_all();
    free(vm);
}

static int push(VM* vm, Value value) {
    if (vm->stack_top >= vm->stack + STACK_MAX) {
        return 0;
    }
    *vm->stack_top = value;
    vm->stack_top++;
    return 1;
}

static int pop(VM* vm, Value* out) {
    if (vm->stack_top <= vm->stack) {
        return 0;
    }
    vm->stack_top--;
    *out = *vm->stack_top;
    return 1;
}

static int binary_op(VM* vm, Value (*op)(Value, Value)) {
    Value b;
    Value a;
    if (!pop(vm, &b)) return 0;
    if (!pop(vm, &a)) return 0;
    Value r = op(a, b);
    int ok = push(vm, r);
    value_release(a);
    value_release(b);
    return ok;
}

Value vm_pop(VM* vm) {
    Value value;
    pop(vm, &value);
    return value;
}

int vm_stack_depth(VM* vm) {
    if (vm == NULL) return 0;
    return (int)(vm->stack_top - vm->stack);
}

Value vm_stack_get(VM* vm, int index) {
    if (vm == NULL || index < 0 || index >= vm_stack_depth(vm)) {
        return value_int(0);
    }
    return vm->stack[index];
}

int vm_local_count(VM* vm) {
    if (vm == NULL) return 0;
    return vm->repl_local_count;
}

Value vm_local_get(VM* vm, int index) {
    if (vm == NULL || index < 0 || index >= vm->repl_local_count) {
        return value_int(0);
    }
    return vm->repl_locals[index];
}

void vm_set_context(VM* vm, struct Context* ctx) {
    if (vm == NULL) return;
    vm->context = ctx;
}

void vm_set_driver(VM* vm, DBDriver* driver) {
    if (vm == NULL) return;
    vm->driver = driver;
}

InterpretResult vm_interpret(VM* vm, Chunk* chunk) {
    vm->chunk = chunk;
    vm->ip = chunk->code;
    vm->error_message[0] = '\0';
    vm->local_count = 0;
    vm->repl_local_count = 0;
    uint8_t* end = chunk->code + chunk->count;

    for (;;) {
        if (vm->ip >= end) {
            set_runtime_error(vm, "Runtime error");
            return INTERPRET_RUNTIME_ERROR;
        }
        uint8_t op = *vm->ip++;
        switch (op) {
            case OP_CONST: {
                if (vm->ip + 2 > end) return INTERPRET_RUNTIME_ERROR;
                uint16_t idx = read_u16(vm->ip);
                vm->ip += 2;
                if (idx >= (uint16_t)vm->chunk->constants_count) {
                    set_runtime_error(vm, "Invalid constant index");
                    return INTERPRET_RUNTIME_ERROR;
                }
                Value v = vm->chunk->constants[idx];
                value_retain(v);
                if (!push(vm, v)) {
                    value_release(v);
                    set_runtime_error(vm, "Stack overflow");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_GET_LOCAL: {
                if (vm->ip + 1 > end) return INTERPRET_RUNTIME_ERROR;
                uint8_t slot = *vm->ip++;
                int depth = (int)(vm->stack_top - vm->frame_base);
                if (slot >= depth) return INTERPRET_RUNTIME_ERROR;
                if (vm->frame_count == 1 && slot + 1 > vm->local_count) {
                    vm->local_count = slot + 1;
                }
                Value v = vm->frame_base[slot];
                value_retain(v);
                if (!push(vm, v)) {
                    value_release(v);
                    set_runtime_error(vm, "Invalid local variable slot");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SET_LOCAL: {
                if (vm->ip + 1 > end) return INTERPRET_RUNTIME_ERROR;
                uint8_t slot = *vm->ip++;
                int depth = (int)(vm->stack_top - vm->frame_base);
                if (slot >= depth) return INTERPRET_RUNTIME_ERROR;
                if (vm->frame_count == 1 && slot + 1 > vm->local_count) {
                    vm->local_count = slot + 1;
                }
                Value v = *(vm->stack_top - 1);
                value_retain(v);
                value_release(vm->frame_base[slot]);
                vm->frame_base[slot] = v;
                if (vm->frame_count == 1) {
                    if (slot < vm->repl_local_count) {
                        value_release(vm->repl_locals[slot]);
                    }
                    vm->repl_locals[slot] = v;
                    value_retain(v);
                    if (slot + 1 > vm->repl_local_count) {
                        vm->repl_local_count = slot + 1;
                    }
                }
                break;
            }
            case OP_ADD: {
                if (!binary_op(vm, value_add)) return INTERPRET_RUNTIME_ERROR;
                break;
            }
            case OP_SUB: {
                if (!binary_op(vm, value_sub)) return INTERPRET_RUNTIME_ERROR;
                break;
            }
            case OP_MUL: {
                if (!binary_op(vm, value_mul)) return INTERPRET_RUNTIME_ERROR;
                break;
            }
            case OP_DIV: {
                if (!binary_op(vm, value_div)) return INTERPRET_RUNTIME_ERROR;
                break;
            }
            case OP_EQ: {
                if (!binary_op(vm, value_eq)) return INTERPRET_RUNTIME_ERROR;
                break;
            }
            case OP_LT: {
                if (!binary_op(vm, value_lt)) return INTERPRET_RUNTIME_ERROR;
                break;
            }
            case OP_GT: {
                if (!binary_op(vm, value_gt)) return INTERPRET_RUNTIME_ERROR;
                break;
            }
            case OP_NEGATE: {
                Value value;
                if (!pop(vm, &value)) return INTERPRET_RUNTIME_ERROR;
                int ok = 0;
                if (value.type == VAL_FLOAT) {
                    ok = push(vm, value_float(-value.as.as_float));
                } else if (value.type == VAL_INT) {
                    ok = push(vm, value_int(-value.as.as_int));
                } else {
                    value_release(value);
                    set_runtime_error(vm, "Cannot negate non-numeric value");
                    return INTERPRET_RUNTIME_ERROR;
                }
                value_release(value);
                if (!ok) return INTERPRET_RUNTIME_ERROR;
                break;
            }
            case OP_NOT: {
                Value value;
                if (!pop(vm, &value)) return INTERPRET_RUNTIME_ERROR;
                int ok = push(vm, value_bool(!value_is_truthy(value)));
                value_release(value);
                if (!ok) return INTERPRET_RUNTIME_ERROR;
                break;
            }
            case OP_POP: {
                Value value;
                if (!pop(vm, &value)) return INTERPRET_RUNTIME_ERROR;
                value_release(value);
                break;
            }
            case OP_SQL: {
                if (vm->ip + 4 > end) return INTERPRET_RUNTIME_ERROR;
                uint16_t idx = read_u16(vm->ip);
                vm->ip += 2;
                uint16_t line = read_u16(vm->ip);
                vm->ip += 2;
                vm->sql_line = (int)line;
                if (idx >= (uint16_t)vm->chunk->constants_count) return INTERPRET_RUNTIME_ERROR;
                Value query_value = vm->chunk->constants[idx];
                if (query_value.type != VAL_STRING || query_value.as.as_string == NULL) {
                    set_runtime_error_sql(vm, "Invalid SQL query");
                    return INTERPRET_RUNTIME_ERROR;
                }
                if (vm->driver != NULL) {
                    if (vm->result_handle != NULL) {
                        vm->driver->result_free(vm->driver, vm->result_handle);
                    }
                    if (!vm->driver->query(vm->driver, query_value.as.as_string,
                                           vm->sql_params, vm->sql_param_count, &vm->result_handle)) {
                        for (int i = 0; i < vm->sql_param_count; i++) {
                            value_release(vm->sql_params[i]);
                        }
                        vm->sql_param_count = 0;
                        set_runtime_error_from_driver_sql(vm, "SQL query failed");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    for (int i = 0; i < vm->sql_param_count; i++) {
                        value_release(vm->sql_params[i]);
                    }
                    vm->sql_param_count = 0;
                } else {
                    Context* ctx = vm->context;
                    if (ctx == NULL || ctx->pager == NULL) {
                        set_runtime_error_sql(vm, "No database context");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    result_free((Result*)vm->result_handle);
                    vm->result_handle = sql_exec(query_value.as.as_string, ctx);
                    for (int i = 0; i < vm->sql_param_count; i++) {
                        value_release(vm->sql_params[i]);
                    }
                    vm->sql_param_count = 0;
                    if (vm->result_handle == NULL) {
                        set_runtime_error_sql(vm, "SQL query failed");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                }
                vm->row_handle = NULL;
                break;
            }
            case OP_SQL_NEXT: {
                if (vm->ip + 2 > end) return INTERPRET_RUNTIME_ERROR;
                int16_t offset = (int16_t)read_u16(vm->ip);
                vm->ip += 2;
                int has_row = 0;
                if (vm->driver != NULL) {
                    void* next = NULL;
                    has_row = vm->driver->result_next(vm->driver, vm->result_handle, &next);
                    vm->row_handle = has_row ? next : NULL;
                } else {
                    Row* next = result_next((Result*)vm->result_handle);
                    has_row = next != NULL;
                    vm->row_handle = next;
                }
                if (!has_row) {
                    uint8_t* target = vm->ip + offset;
                    if (target < vm->chunk->code || target > end) {
                        set_runtime_error(vm, "Invalid SQL query");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm->ip = target;
                }
                break;
            }
            case OP_GET_FIELD: {
                if (vm->ip + 2 > end) return INTERPRET_RUNTIME_ERROR;
                uint16_t idx = read_u16(vm->ip);
                vm->ip += 2;
                if (idx >= (uint16_t)vm->chunk->constants_count) return INTERPRET_RUNTIME_ERROR;
                Value name_value = vm->chunk->constants[idx];
                if (name_value.type != VAL_STRING || name_value.as.as_string == NULL) {
                    set_runtime_error(vm, "Invalid field access");
                    return INTERPRET_RUNTIME_ERROR;
                }
                Value field_value;
                if (vm->driver != NULL) {
                    if (!vm->driver->row_get_field(vm->driver, vm->row_handle,
                                                   name_value.as.as_string, &field_value)) {
                        set_runtime_error_from_driver(vm, "Field access failed");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                } else {
                    Cell cell = row_get_field((Row*)vm->row_handle, name_value.as.as_string);
                    field_value.type = cell.type;
                    if (cell.type == VAL_STRING) {
                        field_value = value_string(strdup(cell.as.as_string));
                    } else if (cell.type == VAL_FLOAT) {
                        field_value.as.as_float = cell.as.as_float;
                    } else {
                        field_value.as.as_int = cell.as.as_int;
                    }
                }
                if (!push(vm, field_value)) return INTERPRET_RUNTIME_ERROR;
                break;
            }
            case OP_JZ: {
                if (vm->ip + 2 > end) return INTERPRET_RUNTIME_ERROR;
                int16_t offset = (int16_t)read_u16(vm->ip);
                vm->ip += 2;
                Value cond;
                if (!pop(vm, &cond)) return INTERPRET_RUNTIME_ERROR;
                int truthy = value_is_truthy(cond);
                value_release(cond);
                if (!truthy) {
                    uint8_t* target = vm->ip + offset;
                    if (target < vm->chunk->code || target > end) {
                        set_runtime_error(vm, "Invalid jump target");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm->ip = target;
                }
                break;
            }
            case OP_JMP: {
                if (vm->ip + 2 > end) return INTERPRET_RUNTIME_ERROR;
                int16_t offset = (int16_t)read_u16(vm->ip);
                vm->ip += 2;
                uint8_t* target = vm->ip + offset;
                if (target < vm->chunk->code || target > end) {
                    set_runtime_error(vm, "Invalid jump target");
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm->ip = target;
                break;
            }
            case OP_CALL: {
                if (vm->ip + 3 > end) return INTERPRET_RUNTIME_ERROR;
                uint16_t target = read_u16(vm->ip);
                vm->ip += 2;
                uint8_t arg_count = *vm->ip++;
                if (target > (uint16_t)vm->chunk->count) return INTERPRET_RUNTIME_ERROR;
                if (arg_count > (size_t)(vm->stack_top - vm->frame_base)) {
                    set_runtime_error(vm, "Invalid argument count");
                    return INTERPRET_RUNTIME_ERROR;
                }
                if (vm->frame_count >= STACK_MAX) return INTERPRET_RUNTIME_ERROR;
                vm->return_ips[vm->frame_count] = vm->ip;
                vm->frames[vm->frame_count] = vm->frame_base;
                vm->frame_count++;
                vm->frame_base = vm->stack_top - arg_count;
                vm->ip = vm->chunk->code + target;
                break;
            }
            case OP_RETURN: {
                if (vm->frame_count == 0) {
                    return INTERPRET_OK;
                }
                Value* callee_frame_base = vm->frame_base;
                Value result = *(vm->stack_top - 1);
                value_retain(result);
                for (Value* p = callee_frame_base; p < vm->stack_top; p++) {
                    value_release(*p);
                }
                vm->frame_count--;
                vm->frame_base = vm->frames[vm->frame_count];
                vm->ip = vm->return_ips[vm->frame_count];
                vm->stack_top = callee_frame_base;
                *vm->stack_top++ = result;
                break;
            }
            case OP_NATIVE_CALL: {
                if (vm->ip + 3 > end) return INTERPRET_RUNTIME_ERROR;
                uint16_t idx = read_u16(vm->ip);
                vm->ip += 2;
                uint8_t argc = *vm->ip++;
                if (argc > MAX_NATIVE_ARGS) {
                    set_runtime_error(vm, "Too many arguments");
                    return INTERPRET_RUNTIME_ERROR;
                }
                if (argc > (size_t)(vm->stack_top - vm->frame_base)) {
                    set_runtime_error(vm, "Invalid argument count");
                    return INTERPRET_RUNTIME_ERROR;
                }
                Value argv[MAX_NATIVE_ARGS];
                for (int i = (int)argc - 1; i >= 0; i--) {
                    if (!pop(vm, &argv[i])) return INTERPRET_RUNTIME_ERROR;
                }
                Value result;
                if (!native_call(vm, idx, argc, argv, &result)) {
                    for (int i = 0; i < (int)argc; i++) value_release(argv[i]);
                    return INTERPRET_RUNTIME_ERROR;
                }
                if (!push(vm, result)) {
                    value_release(result);
                    for (int i = 0; i < (int)argc; i++) value_release(argv[i]);
                    return INTERPRET_RUNTIME_ERROR;
                }
                for (int i = 0; i < (int)argc; i++) value_release(argv[i]);
                break;
            }
            case OP_PRINT: {
                Value value;
                if (!pop(vm, &value)) return INTERPRET_RUNTIME_ERROR;
                value_print(value);
                printf("\n");
                value_release(value);
                break;
            }
            case OP_ARRAY_BUILD: {
                if (vm->ip + 2 > end) return INTERPRET_RUNTIME_ERROR;
                uint16_t count = read_u16(vm->ip);
                vm->ip += 2;
                if (count > (size_t)(vm->stack_top - vm->frame_base)) {
                    set_runtime_error(vm, "Not enough values for array");
                    return INTERPRET_RUNTIME_ERROR;
                }
                ArrayObj* array = array_new();
                if (array == NULL) {
                    set_runtime_error(vm, "Out of memory");
                    return INTERPRET_RUNTIME_ERROR;
                }
                Value* temp = NULL;
                if (count > 0) {
                    temp = malloc(sizeof(Value) * count);
                    if (temp == NULL) {
                        array_free(array);
                        set_runtime_error(vm, "Out of memory");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    for (int i = (int)count - 1; i >= 0; i--) {
                        if (!pop(vm, &temp[i])) {
                            free(temp);
                            array_free(array);
                            return INTERPRET_RUNTIME_ERROR;
                        }
                    }
                    for (int i = 0; i < count; i++) {
                        if (!array_append(array, temp[i])) {
                            free(temp);
                            array_free(array);
                            set_runtime_error(vm, "Out of memory");
                            return INTERPRET_RUNTIME_ERROR;
                        }
                    }
                }
                if (!push(vm, value_array(array))) {
                    array_free(array);
                    if (temp != NULL) free(temp);
                    return INTERPRET_RUNTIME_ERROR;
                }
                for (int i = 0; i < (int)count; i++) {
                    value_release(temp[i]);
                }
                free(temp);
                break;
            }
            case OP_INDEX_GET: {
                Value idx_val;
                Value arr_val;
                if (!pop(vm, &idx_val)) return INTERPRET_RUNTIME_ERROR;
                if (!pop(vm, &arr_val)) return INTERPRET_RUNTIME_ERROR;
                if (arr_val.type != VAL_ARRAY || idx_val.type != VAL_INT) {
                    value_release(idx_val);
                    value_release(arr_val);
                    set_runtime_error(vm, "Invalid array index");
                    return INTERPRET_RUNTIME_ERROR;
                }
                int idx = idx_val.as.as_int;
                if (idx < 0 || idx >= array_length(arr_val.as.as_array)) {
                    value_release(idx_val);
                    value_release(arr_val);
                    set_runtime_error(vm, "Array index out of bounds");
                    return INTERPRET_RUNTIME_ERROR;
                }
                Value result = array_get(arr_val.as.as_array, idx);
                value_retain(result);
                if (!push(vm, result)) {
                    value_release(result);
                    value_release(idx_val);
                    value_release(arr_val);
                    return INTERPRET_RUNTIME_ERROR;
                }
                value_release(idx_val);
                value_release(arr_val);
                break;
            }
            case OP_INDEX_SET: {
                Value val;
                Value idx_val;
                Value arr_val;
                if (!pop(vm, &val)) return INTERPRET_RUNTIME_ERROR;
                if (!pop(vm, &idx_val)) return INTERPRET_RUNTIME_ERROR;
                if (!pop(vm, &arr_val)) return INTERPRET_RUNTIME_ERROR;
                if (arr_val.type != VAL_ARRAY || idx_val.type != VAL_INT) {
                    value_release(val);
                    value_release(idx_val);
                    value_release(arr_val);
                    set_runtime_error(vm, "Invalid array index assignment");
                    return INTERPRET_RUNTIME_ERROR;
                }
                int idx = idx_val.as.as_int;
                if (idx < 0 || idx >= array_length(arr_val.as.as_array)) {
                    value_release(val);
                    value_release(idx_val);
                    value_release(arr_val);
                    set_runtime_error(vm, "Array index out of bounds");
                    return INTERPRET_RUNTIME_ERROR;
                }
                array_set(arr_val.as.as_array, idx, val);
                value_release(val);
                value_release(idx_val);
                value_release(arr_val);
                break;
            }
            case OP_SQL_EXEC: {
                if (vm->ip + 4 > end) return INTERPRET_RUNTIME_ERROR;
                uint16_t idx = read_u16(vm->ip);
                vm->ip += 2;
                uint16_t line = read_u16(vm->ip);
                vm->ip += 2;
                vm->sql_line = (int)line;
                if (idx >= (uint16_t)vm->chunk->constants_count) return INTERPRET_RUNTIME_ERROR;
                Value sql_value = vm->chunk->constants[idx];
                if (sql_value.type != VAL_STRING || sql_value.as.as_string == NULL) {
                    set_runtime_error_sql(vm, "Invalid SQL statement");
                    return INTERPRET_RUNTIME_ERROR;
                }
                if (vm->driver == NULL) {
                    set_runtime_error_sql(vm, "No database driver");
                    return INTERPRET_RUNTIME_ERROR;
                }
                int ok = vm->driver->exec(vm->driver, sql_value.as.as_string,
                                          vm->sql_params, vm->sql_param_count);
                for (int i = 0; i < vm->sql_param_count; i++) {
                    value_release(vm->sql_params[i]);
                }
                vm->sql_param_count = 0;
                if (!ok) {
                    set_runtime_error_from_driver_sql(vm, "SQL execution failed");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SQL_BIND_INT:
            case OP_SQL_BIND_FLOAT:
            case OP_SQL_BIND_STRING: {
                Value v;
                if (!pop(vm, &v)) return INTERPRET_RUNTIME_ERROR;
                if (vm->driver == NULL) {
                    value_release(v);
                    set_runtime_error(vm, "No database driver");
                    return INTERPRET_RUNTIME_ERROR;
                }
                if (vm->sql_param_count >= 16) {
                    value_release(v);
                    set_runtime_error(vm, "Too many SQL parameters");
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm->sql_params[vm->sql_param_count++] = v;
                break;
            }
            case OP_SQL_BEGIN: {
                if (vm->driver == NULL) {
                    set_runtime_error(vm, "No database driver");
                    return INTERPRET_RUNTIME_ERROR;
                }
                if (!vm->driver->begin(vm->driver)) {
                    set_runtime_error_from_driver(vm, "BEGIN failed");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SQL_COMMIT: {
                if (vm->driver == NULL) {
                    set_runtime_error(vm, "No database driver");
                    return INTERPRET_RUNTIME_ERROR;
                }
                if (!vm->driver->commit(vm->driver)) {
                    set_runtime_error_from_driver(vm, "COMMIT failed");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SQL_ROLLBACK: {
                if (vm->driver == NULL) {
                    set_runtime_error(vm, "No database driver");
                    return INTERPRET_RUNTIME_ERROR;
                }
                if (!vm->driver->rollback(vm->driver)) {
                    set_runtime_error_from_driver(vm, "ROLLBACK failed");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_ROW_GET: {
                if (vm->ip + 2 > end) return INTERPRET_RUNTIME_ERROR;
                uint16_t idx = read_u16(vm->ip);
                vm->ip += 2;
                if (idx >= (uint16_t)vm->chunk->constants_count) return INTERPRET_RUNTIME_ERROR;
                Value name_value = vm->chunk->constants[idx];
                if (name_value.type != VAL_STRING || name_value.as.as_string == NULL) {
                    set_runtime_error(vm, "Invalid field access");
                    return INTERPRET_RUNTIME_ERROR;
                }
                Value row_value;
                if (!pop(vm, &row_value)) return INTERPRET_RUNTIME_ERROR;
                if (row_value.type != VAL_ROW || row_value.as.as_row_handle == NULL) {
                    value_release(row_value);
                    set_runtime_error(vm, "Cannot access field on non-row value");
                    return INTERPRET_RUNTIME_ERROR;
                }
                Value field_value = row_obj_get_field((RowObj*)row_value.as.as_row_handle,
                                                      name_value.as.as_string);
                if (!push(vm, field_value)) {
                    value_release(field_value);
                    value_release(row_value);
                    return INTERPRET_RUNTIME_ERROR;
                }
                value_release(row_value);
                break;
            }
            case OP_SQL_GET_COLUMN: {
                if (vm->ip + 2 > end) return INTERPRET_RUNTIME_ERROR;
                uint16_t idx = read_u16(vm->ip);
                vm->ip += 2;
                Value value;
                if (vm->driver != NULL) {
                    if (!vm->driver->row_get_column(vm->driver, vm->row_handle, (int)idx, &value)) {
                        set_runtime_error_from_driver_sql(vm, "Column access failed");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                } else {
                    Row* row = (Row*)vm->row_handle;
                    if (row == NULL || idx >= (uint16_t)row->field_count) {
                        set_runtime_error_sql(vm, "Invalid column index");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    Cell cell = row->fields[idx].value;
                    switch (cell.type) {
                        case VAL_INT:    value = value_int(cell.as.as_int);       break;
                        case VAL_FLOAT:  value = value_float(cell.as.as_float);   break;
                        case VAL_STRING: value = value_string(strdup(cell.as.as_string)); break;
                        default:         value = value_int(0);                    break;
                    }
                }
                if (!push(vm, value)) return INTERPRET_RUNTIME_ERROR;
                break;
            }
            case OP_SQL_TO_ARRAY: {
                ArrayObj* array = array_new();
                if (array == NULL) {
                    set_runtime_error_sql(vm, "out of memory");
                    return INTERPRET_RUNTIME_ERROR;
                }
                if (vm->driver != NULL) {
                    int column_count = vm->driver->result_column_count(vm->driver, vm->result_handle);
                    while (1) {
                        void* row_handle = NULL;
                        int has_row = vm->driver->result_next(vm->driver, vm->result_handle, &row_handle);
                        if (!has_row) break;
                        RowObj* row_obj = row_obj_new(column_count);
                        if (row_obj == NULL) {
                            set_runtime_error_sql(vm, "out of memory");
                            value_release(value_array(array));
                            return INTERPRET_RUNTIME_ERROR;
                        }
                        for (int i = 0; i < column_count; i++) {
                            const char* name = vm->driver->result_column_name(vm->driver, vm->result_handle, i);
                            Value col_value;
                            if (!vm->driver->row_get_column(vm->driver, row_handle, i, &col_value)) {
                                set_runtime_error_from_driver_sql(vm, "Column access failed");
                                row_obj_free(row_obj);
                                value_release(value_array(array));
                                return INTERPRET_RUNTIME_ERROR;
                            }
                            row_obj_set_column(row_obj, i, name, col_value);
                            value_release(col_value);
                        }
                        Value row_value = value_row(row_obj);
                        if (!array_append(array, row_value)) {
                            value_release(row_value);
                            value_release(value_array(array));
                            set_runtime_error_sql(vm, "out of memory");
                            return INTERPRET_RUNTIME_ERROR;
                        }
                        value_release(row_value);
                    }
                    vm->driver->result_free(vm->driver, vm->result_handle);
                } else {
                    Context* ctx = vm->context;
                    if (ctx == NULL || ctx->pager == NULL) {
                        set_runtime_error_sql(vm, "No database context");
                        value_release(value_array(array));
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    Result* res = (Result*)vm->result_handle;
                    while (1) {
                        Row* row = result_next(res);
                        if (row == NULL) break;
                        RowObj* row_obj = row_obj_new(row->field_count);
                        if (row_obj == NULL) {
                            set_runtime_error_sql(vm, "out of memory");
                            value_release(value_array(array));
                            return INTERPRET_RUNTIME_ERROR;
                        }
                        for (int i = 0; i < row->field_count; i++) {
                            Cell cell = row->fields[i].value;
                            Value col_value;
                            switch (cell.type) {
                                case VAL_INT:    col_value = value_int(cell.as.as_int);       break;
                                case VAL_FLOAT:  col_value = value_float(cell.as.as_float);   break;
                                case VAL_STRING: col_value = value_string(strdup(cell.as.as_string)); break;
                                default:         col_value = value_int(0);                    break;
                            }
                            row_obj_set_column(row_obj, i, row->fields[i].name, col_value);
                            value_release(col_value);
                        }
                        Value row_value = value_row(row_obj);
                        if (!array_append(array, row_value)) {
                            value_release(row_value);
                            value_release(value_array(array));
                            set_runtime_error_sql(vm, "out of memory");
                            return INTERPRET_RUNTIME_ERROR;
                        }
                        value_release(row_value);
                    }
                    result_free(res);
                }
                vm->result_handle = NULL;
                vm->row_handle = NULL;
                if (!push(vm, value_array(array))) {
                    value_release(value_array(array));
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_RUNTIME_ERROR: {
                if (vm->ip + 2 > end) return INTERPRET_RUNTIME_ERROR;
                uint16_t idx = read_u16(vm->ip);
                vm->ip += 2;
                if (idx >= (uint16_t)vm->chunk->constants_count) return INTERPRET_RUNTIME_ERROR;
                Value msg_value = vm->chunk->constants[idx];
                if (msg_value.type != VAL_STRING || msg_value.as.as_string == NULL) {
                    set_runtime_error(vm, "Runtime error");
                } else {
                    set_runtime_error(vm, msg_value.as.as_string);
                }
                return INTERPRET_RUNTIME_ERROR;
            }
            default:
                set_runtime_error(vm, "Unknown opcode");
                return INTERPRET_RUNTIME_ERROR;
        }
    }
}
