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
    Result*       result;
    Row*          row;
    struct Context* context;
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
    vm->result = NULL;
    vm->row = NULL;
    vm->context = NULL;
    vm->error_message[0] = '\0';
    return vm;
}

static void set_runtime_error(VM* vm, const char* message) {
    snprintf(vm->error_message, sizeof(vm->error_message), "%s", message);
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
    result_free(vm->result);
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

void vm_set_context(VM* vm, struct Context* ctx) {
    if (vm == NULL) return;
    vm->context = ctx;
}

InterpretResult vm_interpret(VM* vm, Chunk* chunk) {
    vm->chunk = chunk;
    vm->ip = chunk->code;
    vm->error_message[0] = '\0';
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
                Value v = *(vm->stack_top - 1);
                value_retain(v);
                value_release(vm->frame_base[slot]);
                vm->frame_base[slot] = v;
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
                if (vm->ip + 2 > end) return INTERPRET_RUNTIME_ERROR;
                uint16_t idx = read_u16(vm->ip);
                vm->ip += 2;
                if (idx >= (uint16_t)vm->chunk->constants_count) return INTERPRET_RUNTIME_ERROR;
                Value query_value = vm->chunk->constants[idx];
                if (query_value.type != VAL_STRING || query_value.as.as_string == NULL) {
                    set_runtime_error(vm, "Invalid SQL query");
                    return INTERPRET_RUNTIME_ERROR;
                }
                Context* ctx = vm->context;
                if (ctx == NULL || ctx->pager == NULL) return INTERPRET_RUNTIME_ERROR;
                result_free(vm->result);
                vm->result = sql_exec(query_value.as.as_string, ctx);
                if (vm->result == NULL) return INTERPRET_RUNTIME_ERROR;
                vm->row = NULL;
                break;
            }
            case OP_SQL_NEXT: {
                if (vm->ip + 2 > end) return INTERPRET_RUNTIME_ERROR;
                int16_t offset = (int16_t)read_u16(vm->ip);
                vm->ip += 2;
                Row* next = result_next(vm->result);
                if (next == NULL) {
                    uint8_t* target = vm->ip + offset;
                    if (target < vm->chunk->code || target > end) {
                        set_runtime_error(vm, "Invalid SQL query");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm->ip = target;
                } else {
                    vm->row = next;
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
                Cell cell = row_get_field(vm->row, name_value.as.as_string);
                Value field_value;
                field_value.type = cell.type;
                if (cell.type == VAL_STRING) {
                    field_value = value_string(strdup(cell.as.as_string));
                } else if (cell.type == VAL_FLOAT) {
                    field_value.as.as_float = cell.as.as_float;
                } else {
                    field_value.as.as_int = cell.as.as_int;
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
                Value result = *(vm->stack_top - 1);
                for (Value* p = vm->frame_base; p < vm->stack_top - 1; p++) {
                    value_release(*p);
                }
                vm->frame_count--;
                vm->frame_base = vm->frames[vm->frame_count];
                vm->ip = vm->return_ips[vm->frame_count];
                vm->stack_top = vm->frame_base;
                if (!push(vm, result)) return INTERPRET_RUNTIME_ERROR;
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
            default:
                set_runtime_error(vm, "Unknown opcode");
                return INTERPRET_RUNTIME_ERROR;
        }
    }
}
