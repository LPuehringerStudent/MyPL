#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vm.h"
#include "diagnostics.h"
#include "natives.h"
#include "sql_engine.h"

#define TRY_MAX 64
#define MAX_OUT_PARAMS 16

typedef struct {
    uint8_t* catch_ip;
    int      frame_count;
    Value*   frame_base;
    int      local_count;
    Value*   stack_top;
} TryFrame;

typedef struct {
    int out_count;
    int out_positions[MAX_OUT_PARAMS];
    int out_slots[MAX_OUT_PARAMS];
} OutParamFrame;

struct VM {
    Chunk*        chunk;
    uint8_t*      ip;
    Value         stack[STACK_MAX];
    Value*        stack_top;
    Value*        frames[STACK_MAX];
    uint8_t*      return_ips[STACK_MAX];
    int           frame_count;
    Value*        frame_base;
    OutParamFrame out_frames[STACK_MAX];
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
    int           sql_rowcount;
    TryFrame      try_frames[TRY_MAX];
    int           try_count;
    char          error_message[256];
    int           sql_code;
    char          sql_errm[256];
    Value         globals[256];
    int           global_count;
    int           dbms_output_enabled;
    int           dbms_output_limit;
    ArrayObj*     dbms_output_buffer;
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
    vm->sql_rowcount = 0;
    vm->try_count = 0;
    vm->error_message[0] = '\0';
    vm->sql_code = 0;
    vm->sql_errm[0] = '\0';
    vm->global_count = 0;
    for (int i = 0; i < 256; i++) {
        vm->globals[i].type = VAL_INT;
        vm->globals[i].as.as_int = 0;
    }
    vm->dbms_output_enabled = 0;
    vm->dbms_output_limit = 0;
    vm->dbms_output_buffer = NULL;
    return vm;
}

static int current_line(VM* vm) {
    if (vm->chunk == NULL || vm->chunk->lines == NULL || vm->ip == NULL) return 0;
    int offset = (int)(vm->ip - vm->chunk->code) - 1;
    if (offset < 0 || offset >= vm->chunk->lines_count) return 0;
    return vm->chunk->lines[offset];
}

static int current_column(VM* vm) {
    if (vm->chunk == NULL || vm->chunk->columns == NULL || vm->ip == NULL) return 0;
    int offset = (int)(vm->ip - vm->chunk->code) - 1;
    if (offset < 0 || offset >= vm->chunk->columns_count) return 0;
    return vm->chunk->columns[offset];
}

static void set_runtime_error_ex(VM* vm, const char* message, int code) {
    format_error(vm->error_message, sizeof(vm->error_message),
                 vm->chunk != NULL ? vm->chunk->source_path : NULL,
                 current_line(vm), current_column(vm), message);
    vm->sql_code = code;
    strncpy(vm->sql_errm, vm->error_message, sizeof(vm->sql_errm) - 1);
    vm->sql_errm[sizeof(vm->sql_errm) - 1] = '\0';
}

static void set_runtime_error(VM* vm, const char* message) {
    set_runtime_error_ex(vm, message, 1);
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

/* Note: this helper is used by natives and other callers that want to set an
 * error from C code. It uses the current bytecode location when available. */

static void set_runtime_error_sql(VM* vm, const char* message) {
    char msg[512];
    snprintf(msg, sizeof(msg), "SQL error: %s", message);
    set_runtime_error_ex(vm, msg, 1);
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

void vm_set_error_with_code(VM* vm, const char* message, int code) {
    if (vm == NULL) return;
    set_runtime_error_ex(vm, message, code);
}

DBDriver* vm_get_driver(VM* vm) {
    if (vm == NULL) return NULL;
    return vm->driver;
}

void vm_set_sql_rowcount(VM* vm, int rowcount) {
    if (vm == NULL) return;
    vm->sql_rowcount = rowcount;
}

int vm_get_sql_rowcount(VM* vm) {
    if (vm == NULL) return 0;
    return vm->sql_rowcount;
}

int vm_get_sql_found(VM* vm) {
    if (vm == NULL) return 0;
    return vm->sql_rowcount > 0;
}

int vm_get_sql_notfound(VM* vm) {
    if (vm == NULL) return 0;
    return vm->sql_rowcount == 0;
}

int vm_get_sql_code(VM* vm) {
    if (vm == NULL) return 0;
    return vm->sql_code;
}

const char* vm_get_sql_errm(VM* vm) {
    if (vm == NULL) return "";
    return vm->sql_errm;
}

void vm_dbms_output_enable(VM* vm, int limit) {
    if (vm == NULL) return;
    vm->dbms_output_enabled = 1;
    vm->dbms_output_limit = limit;
    if (vm->dbms_output_buffer != NULL) {
        array_free(vm->dbms_output_buffer);
    }
    vm->dbms_output_buffer = array_new();
}

void vm_dbms_output_put_line(VM* vm, Value line) {
    if (vm == NULL || !vm->dbms_output_enabled) return;
    if (vm->dbms_output_buffer == NULL) {
        vm->dbms_output_buffer = array_new();
    }
    if (vm->dbms_output_limit > 0 && array_length(vm->dbms_output_buffer) >= vm->dbms_output_limit) {
        set_runtime_error(vm, "dbms_output buffer full");
        return;
    }
    value_retain(line);
    array_append(vm->dbms_output_buffer, line);
}

void vm_dbms_output_disable(VM* vm) {
    if (vm == NULL) return;
    vm->dbms_output_enabled = 0;
    if (vm->dbms_output_buffer != NULL) {
        array_free(vm->dbms_output_buffer);
        vm->dbms_output_buffer = NULL;
    }
}

Value vm_dbms_output_get_lines(VM* vm) {
    if (vm == NULL || vm->dbms_output_buffer == NULL) {
        return value_array(array_new());
    }
    ArrayObj* result = array_new();
    int n = array_length(vm->dbms_output_buffer);
    for (int i = 0; i < n; i++) {
        Value v = array_get(vm->dbms_output_buffer, i);
        value_retain(v);
        array_append(result, v);
    }
    array_free(vm->dbms_output_buffer);
    vm->dbms_output_buffer = array_new();
    return value_array(result);
}

static int push(VM* vm, Value value);

static int vm_catch(VM* vm) {
    if (vm->try_count == 0) return 0;
    TryFrame* tf = &vm->try_frames[--vm->try_count];
    /* Preserve locals that existed before the try block (slots below catch_var).
     * local_count includes the catch variable slot, so preserve_top points at
     * the slot where the error message will live. */
    Value* preserve_top = tf->frame_base + (tf->local_count - 1);
    for (Value* p = vm->stack_top - 1; p >= preserve_top; p--) {
        value_release(*p);
    }
    vm->stack_top = preserve_top;
    vm->frame_count = tf->frame_count;
    vm->frame_base = tf->frame_base;
    const char* msg = vm->error_message[0] != '\0' ? vm->error_message : "Runtime error";
    char* copy = strdup(msg);
    if (copy == NULL) {
        set_runtime_error(vm, "Out of memory");
        return 0;
    }
    Value msg_value = value_string(copy);
    if (!push(vm, msg_value)) {
        value_release(msg_value);
        return 0;
    }
    vm->ip = tf->catch_ip;
    vm->error_message[0] = '\0';
    return 1;
}

#define THROW(vm) do { \
    if (!vm_catch(vm)) return INTERPRET_RUNTIME_ERROR; \
    goto dispatch; \
} while (0)

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
    if (vm->dbms_output_buffer != NULL) {
        array_free(vm->dbms_output_buffer);
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

static int vm_call_autonomous(VM* parent, uint16_t target, uint8_t arg_count,
                              int out_count, int* out_positions, int* out_slots,
                              Value* out_result);

static InterpretResult vm_run(VM* vm, uint8_t* end) {
    for (;;) {
dispatch:
        if (vm->ip >= end) {
            set_runtime_error(vm, "Runtime error");
            THROW(vm);
        }
        uint8_t op = *vm->ip++;
        switch (op) {
            case OP_CONST: {
                if (vm->ip + 2 > end) return INTERPRET_RUNTIME_ERROR;
                uint16_t idx = read_u16(vm->ip);
                vm->ip += 2;
                if (idx >= (uint16_t)vm->chunk->constants_count) {
                    set_runtime_error(vm, "Invalid constant index");
                    THROW(vm);
                }
                Value v = vm->chunk->constants[idx];
                value_retain(v);
                if (!push(vm, v)) {
                    value_release(v);
                    set_runtime_error(vm, "Stack overflow");
                    THROW(vm);
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
                    THROW(vm);
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
            case OP_GET_GLOBAL: {
                if (vm->ip + 1 > end) return INTERPRET_RUNTIME_ERROR;
                uint8_t slot = *vm->ip++;
                if (slot >= (size_t)vm->global_count) return INTERPRET_RUNTIME_ERROR;
                Value v = vm->globals[slot];
                value_retain(v);
                if (!push(vm, v)) {
                    value_release(v);
                    set_runtime_error(vm, "Stack overflow");
                    THROW(vm);
                }
                break;
            }
            case OP_SET_GLOBAL: {
                if (vm->ip + 1 > end) return INTERPRET_RUNTIME_ERROR;
                uint8_t slot = *vm->ip++;
                if (slot >= (size_t)vm->global_count) {
                    vm->global_count = slot + 1;
                }
                Value v = *(vm->stack_top - 1);
                value_retain(v);
                value_release(vm->globals[slot]);
                vm->globals[slot] = v;
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
                    THROW(vm);
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
            case OP_DUP: {
                if (vm->stack_top <= vm->stack) {
                    set_runtime_error(vm, "Stack underflow");
                    return INTERPRET_RUNTIME_ERROR;
                }
                Value value = *(vm->stack_top - 1);
                value_retain(value);
                if (!push(vm, value)) {
                    value_release(value);
                    return INTERPRET_RUNTIME_ERROR;
                }
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
                    THROW(vm);
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
                        THROW(vm);
                    }
                    for (int i = 0; i < vm->sql_param_count; i++) {
                        value_release(vm->sql_params[i]);
                    }
                    vm->sql_param_count = 0;
                } else {
                    Context* ctx = vm->context;
                    if (ctx == NULL || ctx->pager == NULL) {
                        set_runtime_error_sql(vm, "No database context");
                        THROW(vm);
                    }
                    result_free((Result*)vm->result_handle);
                    vm->result_handle = sql_exec(query_value.as.as_string, ctx);
                    for (int i = 0; i < vm->sql_param_count; i++) {
                        value_release(vm->sql_params[i]);
                    }
                    vm->sql_param_count = 0;
                    if (vm->result_handle == NULL) {
                        set_runtime_error_sql(vm, "SQL query failed");
                        THROW(vm);
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
                        THROW(vm);
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
                    THROW(vm);
                }
                Value field_value;
                if (vm->driver != NULL) {
                    if (!vm->driver->row_get_field(vm->driver, vm->row_handle,
                                                   name_value.as.as_string, &field_value)) {
                        set_runtime_error_from_driver(vm, "Field access failed");
                        THROW(vm);
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
                        THROW(vm);
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
                    THROW(vm);
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
                    THROW(vm);
                }
                if (vm->frame_count >= STACK_MAX) return INTERPRET_RUNTIME_ERROR;
                vm->out_frames[vm->frame_count].out_count = 0;
                vm->return_ips[vm->frame_count] = vm->ip;
                vm->frames[vm->frame_count] = vm->frame_base;
                vm->frame_count++;
                vm->frame_base = vm->stack_top - arg_count;
                vm->ip = vm->chunk->code + target;
                break;
            }
            case OP_CALL_AUTONOMOUS: {
                if (vm->ip + 3 > end) return INTERPRET_RUNTIME_ERROR;
                uint16_t target = read_u16(vm->ip);
                vm->ip += 2;
                uint8_t arg_count = *vm->ip++;
                if (arg_count > (size_t)(vm->stack_top - vm->frame_base)) {
                    set_runtime_error(vm, "Invalid argument count");
                    THROW(vm);
                }
                Value result;
                int auto_result = vm_call_autonomous(vm, target, arg_count, 0, NULL, NULL, &result);
                if (auto_result == 1) {
                    for (int i = 0; i < arg_count; i++) {
                        Value dummy;
                        pop(vm, &dummy);
                        value_release(dummy);
                    }
                    if (!push(vm, result)) {
                        value_release(result);
                        return INTERPRET_RUNTIME_ERROR;
                    }
                } else if (auto_result == -1) {
                    if (vm->frame_count >= STACK_MAX) return INTERPRET_RUNTIME_ERROR;
                    vm->out_frames[vm->frame_count].out_count = 0;
                    vm->return_ips[vm->frame_count] = vm->ip;
                    vm->frames[vm->frame_count] = vm->frame_base;
                    vm->frame_count++;
                    vm->frame_base = vm->stack_top - arg_count;
                    vm->ip = vm->chunk->code + target;
                } else {
                    THROW(vm);
                }
                break;
            }
            case OP_CALL_OUT: {
                if (vm->ip + 4 > end) return INTERPRET_RUNTIME_ERROR;
                uint16_t target = read_u16(vm->ip);
                vm->ip += 2;
                uint8_t arg_count = *vm->ip++;
                uint8_t out_count = *vm->ip++;
                if (target > (uint16_t)vm->chunk->count) return INTERPRET_RUNTIME_ERROR;
                if (arg_count > (size_t)(vm->stack_top - vm->frame_base)) {
                    set_runtime_error(vm, "Invalid argument count");
                    THROW(vm);
                }
                if (out_count > MAX_OUT_PARAMS) {
                    set_runtime_error(vm, "Too many OUT/IN OUT parameters");
                    THROW(vm);
                }
                if (vm->ip + out_count * 2 > end) return INTERPRET_RUNTIME_ERROR;
                if (vm->frame_count >= STACK_MAX) return INTERPRET_RUNTIME_ERROR;
                OutParamFrame* out_frame = &vm->out_frames[vm->frame_count];
                out_frame->out_count = (int)out_count;
                for (int i = 0; i < out_count; i++) {
                    out_frame->out_positions[i] = *vm->ip++;
                    out_frame->out_slots[i] = *vm->ip++;
                }
                vm->return_ips[vm->frame_count] = vm->ip;
                vm->frames[vm->frame_count] = vm->frame_base;
                vm->frame_count++;
                vm->frame_base = vm->stack_top - arg_count;
                vm->ip = vm->chunk->code + target;
                break;
            }
            case OP_CALL_OUT_AUTONOMOUS: {
                if (vm->ip + 4 > end) return INTERPRET_RUNTIME_ERROR;
                uint16_t target = read_u16(vm->ip);
                vm->ip += 2;
                uint8_t arg_count = *vm->ip++;
                uint8_t out_count = *vm->ip++;
                if (target > (uint16_t)vm->chunk->count) return INTERPRET_RUNTIME_ERROR;
                if (arg_count > (size_t)(vm->stack_top - vm->frame_base)) {
                    set_runtime_error(vm, "Invalid argument count");
                    THROW(vm);
                }
                if (out_count > MAX_OUT_PARAMS) {
                    set_runtime_error(vm, "Too many OUT/IN OUT parameters");
                    THROW(vm);
                }
                if (vm->ip + out_count * 2 > end) return INTERPRET_RUNTIME_ERROR;
                int out_positions[MAX_OUT_PARAMS];
                int out_slots[MAX_OUT_PARAMS];
                for (int i = 0; i < out_count; i++) {
                    out_positions[i] = *vm->ip++;
                    out_slots[i] = *vm->ip++;
                }
                Value result;
                int auto_result = vm_call_autonomous(vm, target, arg_count,
                                                     (int)out_count, out_positions, out_slots,
                                                     &result);
                if (auto_result == 1) {
                    for (int i = 0; i < arg_count; i++) {
                        Value dummy;
                        pop(vm, &dummy);
                        value_release(dummy);
                    }
                    if (!push(vm, result)) {
                        value_release(result);
                        return INTERPRET_RUNTIME_ERROR;
                    }
                } else if (auto_result == -1) {
                    if (vm->frame_count >= STACK_MAX) return INTERPRET_RUNTIME_ERROR;
                    OutParamFrame* out_frame = &vm->out_frames[vm->frame_count];
                    out_frame->out_count = (int)out_count;
                    for (int i = 0; i < out_count; i++) {
                        out_frame->out_positions[i] = out_positions[i];
                        out_frame->out_slots[i] = out_slots[i];
                    }
                    vm->return_ips[vm->frame_count] = vm->ip;
                    vm->frames[vm->frame_count] = vm->frame_base;
                    vm->frame_count++;
                    vm->frame_base = vm->stack_top - arg_count;
                    vm->ip = vm->chunk->code + target;
                } else {
                    THROW(vm);
                }
                break;
            }
            case OP_RETURN: {
                if (vm->frame_count == 0) {
                    return INTERPRET_OK;
                }
                Value* callee_frame_base = vm->frame_base;
                Value result = *(vm->stack_top - 1);
                value_retain(result);
                int callee_stack_size = (int)(vm->stack_top - callee_frame_base);
                OutParamFrame* out_frame = &vm->out_frames[vm->frame_count - 1];
                Value out_values[MAX_OUT_PARAMS];
                for (int i = 0; i < out_frame->out_count; i++) {
                    int pos = out_frame->out_positions[i];
                    if (pos < 0 || pos >= callee_stack_size) {
                        set_runtime_error(vm, "Invalid OUT parameter position");
                        THROW(vm);
                    }
                    out_values[i] = callee_frame_base[pos];
                    value_retain(out_values[i]);
                }
                for (Value* p = callee_frame_base; p < vm->stack_top; p++) {
                    value_release(*p);
                }
                vm->frame_count--;
                vm->frame_base = vm->frames[vm->frame_count];
                vm->ip = vm->return_ips[vm->frame_count];
                vm->stack_top = callee_frame_base;
                for (int i = 0; i < out_frame->out_count; i++) {
                    int slot = out_frame->out_slots[i];
                    if (slot < 0 || slot >= (vm->stack_top - vm->frame_base)) {
                        set_runtime_error(vm, "Invalid OUT parameter slot");
                        value_release(out_values[i]);
                        THROW(vm);
                    }
                    value_release(vm->frame_base[slot]);
                    vm->frame_base[slot] = out_values[i];
                }
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
                    THROW(vm);
                }
                if (argc > (size_t)(vm->stack_top - vm->frame_base)) {
                    set_runtime_error(vm, "Invalid argument count");
                    THROW(vm);
                }
                Value argv[MAX_NATIVE_ARGS];
                for (int i = (int)argc - 1; i >= 0; i--) {
                    if (!pop(vm, &argv[i])) {
                        set_runtime_error(vm, "Stack underflow");
                        THROW(vm);
                    }
                }
                Value result;
                if (!native_call(vm, idx, argc, argv, &result)) {
                    for (int i = 0; i < (int)argc; i++) value_release(argv[i]);
                    THROW(vm);
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
                    THROW(vm);
                }
                ArrayObj* array = array_new();
                if (array == NULL) {
                    set_runtime_error(vm, "Out of memory");
                    THROW(vm);
                }
                Value* temp = NULL;
                if (count > 0) {
                    temp = malloc(sizeof(Value) * count);
                    if (temp == NULL) {
                        array_free(array);
                        set_runtime_error(vm, "Out of memory");
                        THROW(vm);
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
                            THROW(vm);
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
            case OP_MAP_BUILD: {
                if (vm->ip + 2 > end) return INTERPRET_RUNTIME_ERROR;
                uint16_t count = read_u16(vm->ip);
                vm->ip += 2;
                if (count * 2 > (size_t)(vm->stack_top - vm->frame_base)) {
                    set_runtime_error(vm, "Not enough values for map");
                    THROW(vm);
                }
                MapObj* map = map_new();
                if (map == NULL) {
                    set_runtime_error(vm, "Out of memory");
                    THROW(vm);
                }
                for (int i = (int)count - 1; i >= 0; i--) {
                    Value key;
                    Value val;
                    if (!pop(vm, &key)) {
                        map_free(map);
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    if (!pop(vm, &val)) {
                        value_release(key);
                        map_free(map);
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    if (key.type != VAL_INT && key.type != VAL_STRING) {
                        value_release(key);
                        value_release(val);
                        map_free(map);
                        set_runtime_error(vm, "Map key must be int or string");
                        THROW(vm);
                    }
                    if (!map_set(map, key, val)) {
                        value_release(key);
                        value_release(val);
                        map_free(map);
                        set_runtime_error(vm, "Out of memory");
                        THROW(vm);
                    }
                    value_release(key);
                    value_release(val);
                }
                Value result = value_map(map);
                if (!push(vm, result)) {
                    value_release(result);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_INDEX_GET: {
                Value idx_val;
                Value arr_val;
                if (!pop(vm, &idx_val)) return INTERPRET_RUNTIME_ERROR;
                if (!pop(vm, &arr_val)) return INTERPRET_RUNTIME_ERROR;
                if (arr_val.type == VAL_ARRAY && idx_val.type == VAL_INT) {
                    int idx = idx_val.as.as_int;
                    if (idx < 0 || idx >= array_length(arr_val.as.as_array)) {
                        value_release(idx_val);
                        value_release(arr_val);
                        set_runtime_error(vm, "Array index out of bounds");
                        THROW(vm);
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
                if (arr_val.type == VAL_ROW && idx_val.type == VAL_STRING) {
                    Value result = row_obj_get_field((RowObj*)arr_val.as.as_row_handle,
                                                     idx_val.as.as_string);
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
                if (arr_val.type == VAL_MAP) {
                    if (idx_val.type != VAL_INT && idx_val.type != VAL_STRING) {
                        value_release(idx_val);
                        value_release(arr_val);
                        set_runtime_error(vm, "Map key must be int or string");
                        THROW(vm);
                    }
                    Value result;
                    if (!map_get(arr_val.as.as_map, idx_val, &result)) {
                        value_release(idx_val);
                        value_release(arr_val);
                        set_runtime_error(vm, "Key not found in map");
                        THROW(vm);
                    }
                    if (!push(vm, result)) {
                        value_release(result);
                        value_release(idx_val);
                        value_release(arr_val);
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    value_release(result);
                    value_release(idx_val);
                    value_release(arr_val);
                    break;
                }
                value_release(idx_val);
                value_release(arr_val);
                set_runtime_error(vm, "Invalid index");
                THROW(vm);
                break;
            }
            case OP_INDEX_SET: {
                Value val;
                Value idx_val;
                Value arr_val;
                if (!pop(vm, &val)) return INTERPRET_RUNTIME_ERROR;
                if (!pop(vm, &idx_val)) return INTERPRET_RUNTIME_ERROR;
                if (!pop(vm, &arr_val)) return INTERPRET_RUNTIME_ERROR;
                if (arr_val.type == VAL_ARRAY && idx_val.type == VAL_INT) {
                    int idx = idx_val.as.as_int;
                    if (idx < 0 || idx >= array_length(arr_val.as.as_array)) {
                        value_release(val);
                        value_release(idx_val);
                        value_release(arr_val);
                        set_runtime_error(vm, "Array index out of bounds");
                        THROW(vm);
                    }
                    array_set(arr_val.as.as_array, idx, val);
                    value_release(val);
                    value_release(idx_val);
                    value_release(arr_val);
                    break;
                }
                if (arr_val.type == VAL_ROW && idx_val.type == VAL_STRING) {
                    RowObj* row = (RowObj*)arr_val.as.as_row_handle;
                    if (!row_obj_set_field(row, idx_val.as.as_string, val)) {
                        value_release(val);
                        value_release(idx_val);
                        value_release(arr_val);
                        set_runtime_error(vm, "Map key not found");
                        THROW(vm);
                    }
                    value_release(val);
                    value_release(idx_val);
                    value_release(arr_val);
                    break;
                }
                if (arr_val.type == VAL_MAP) {
                    if (idx_val.type != VAL_INT && idx_val.type != VAL_STRING) {
                        value_release(val);
                        value_release(idx_val);
                        value_release(arr_val);
                        set_runtime_error(vm, "Map key must be int or string");
                        THROW(vm);
                    }
                    if (!map_set(arr_val.as.as_map, idx_val, val)) {
                        value_release(val);
                        value_release(idx_val);
                        value_release(arr_val);
                        set_runtime_error(vm, "Out of memory");
                        THROW(vm);
                    }
                    value_release(val);
                    value_release(idx_val);
                    value_release(arr_val);
                    break;
                }
                value_release(val);
                value_release(idx_val);
                value_release(arr_val);
                set_runtime_error(vm, "Invalid index assignment");
                THROW(vm);
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
                    THROW(vm);
                }
                if (vm->driver == NULL) {
                    set_runtime_error_sql(vm, "No database driver");
                    THROW(vm);
                }
                int row_count = vm->driver->exec(vm->driver, sql_value.as.as_string,
                                                 vm->sql_params, vm->sql_param_count);
                for (int i = 0; i < vm->sql_param_count; i++) {
                    value_release(vm->sql_params[i]);
                }
                vm->sql_param_count = 0;
                if (row_count < 0) {
                    set_runtime_error_from_driver_sql(vm, "SQL execution failed");
                    THROW(vm);
                }
                vm->sql_rowcount = row_count;
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
                    THROW(vm);
                }
                if (vm->sql_param_count >= 16) {
                    value_release(v);
                    set_runtime_error(vm, "Too many SQL parameters");
                    THROW(vm);
                }
                vm->sql_params[vm->sql_param_count++] = v;
                break;
            }
            case OP_SQL_BEGIN: {
                if (vm->driver == NULL) {
                    set_runtime_error(vm, "No database driver");
                    THROW(vm);
                }
                if (!vm->driver->begin(vm->driver)) {
                    set_runtime_error_from_driver(vm, "BEGIN failed");
                    THROW(vm);
                }
                break;
            }
            case OP_SQL_COMMIT: {
                if (vm->driver == NULL) {
                    set_runtime_error(vm, "No database driver");
                    THROW(vm);
                }
                if (!vm->driver->commit(vm->driver)) {
                    set_runtime_error_from_driver(vm, "COMMIT failed");
                    THROW(vm);
                }
                break;
            }
            case OP_SQL_ROLLBACK: {
                if (vm->driver == NULL) {
                    set_runtime_error(vm, "No database driver");
                    THROW(vm);
                }
                if (!vm->driver->rollback(vm->driver)) {
                    set_runtime_error_from_driver(vm, "ROLLBACK failed");
                    THROW(vm);
                }
                break;
            }
            case OP_SQL_SAVEPOINT:
            case OP_SQL_ROLLBACK_TO:
            case OP_SQL_RELEASE_SAVEPOINT: {
                if (vm->ip + 2 > end) return INTERPRET_RUNTIME_ERROR;
                uint16_t idx = read_u16(vm->ip);
                vm->ip += 2;
                if (idx >= (uint16_t)vm->chunk->constants_count) {
                    set_runtime_error(vm, "Invalid constant index");
                    THROW(vm);
                }
                Value name_value = vm->chunk->constants[idx];
                if (name_value.type != VAL_STRING || name_value.as.as_string == NULL) {
                    set_runtime_error(vm, "Invalid savepoint name");
                    THROW(vm);
                }
                if (vm->driver == NULL) {
                    set_runtime_error(vm, "No database driver");
                    THROW(vm);
                }
                int ok = 0;
                if (op == OP_SQL_SAVEPOINT) {
                    ok = vm->driver->savepoint(vm->driver, name_value.as.as_string);
                } else if (op == OP_SQL_ROLLBACK_TO) {
                    ok = vm->driver->rollback_to_savepoint(vm->driver, name_value.as.as_string);
                } else {
                    ok = vm->driver->release_savepoint(vm->driver, name_value.as.as_string);
                }
                if (!ok) {
                    const char* op_name = (op == OP_SQL_SAVEPOINT) ? "SAVEPOINT" :
                                          (op == OP_SQL_ROLLBACK_TO) ? "ROLLBACK TO SAVEPOINT" :
                                          "RELEASE SAVEPOINT";
                    set_runtime_error_from_driver(vm, op_name);
                    THROW(vm);
                }
                break;
            }
            case OP_CURSOR_OPEN: {
                if (vm->ip + 4 > end) return INTERPRET_RUNTIME_ERROR;
                uint16_t idx = read_u16(vm->ip);
                vm->ip += 2;
                uint16_t line = read_u16(vm->ip);
                vm->ip += 2;
                vm->sql_line = (int)line;
                Value old_cursor;
                if (!pop(vm, &old_cursor)) return INTERPRET_RUNTIME_ERROR;
                if (old_cursor.type != VAL_CURSOR) {
                    value_release(old_cursor);
                    set_runtime_error_sql(vm, "OPEN requires a cursor variable");
                    THROW(vm);
                }
                value_release(old_cursor);
                if (idx >= (uint16_t)vm->chunk->constants_count) return INTERPRET_RUNTIME_ERROR;
                Value query_value = vm->chunk->constants[idx];
                if (query_value.type != VAL_STRING || query_value.as.as_string == NULL) {
                    set_runtime_error_sql(vm, "Invalid SQL query");
                    THROW(vm);
                }
                CursorObj* cursor = cursor_obj_new(vm->driver);
                if (cursor == NULL) {
                    set_runtime_error_sql(vm, "out of memory");
                    THROW(vm);
                }
                if (vm->driver != NULL) {
                    if (!vm->driver->query(vm->driver, query_value.as.as_string,
                                           vm->sql_params, vm->sql_param_count, &cursor->result_handle)) {
                        for (int i = 0; i < vm->sql_param_count; i++) {
                            value_release(vm->sql_params[i]);
                        }
                        vm->sql_param_count = 0;
                        value_release(value_cursor(cursor));
                        set_runtime_error_from_driver_sql(vm, "SQL query failed");
                        THROW(vm);
                    }
                    for (int i = 0; i < vm->sql_param_count; i++) {
                        value_release(vm->sql_params[i]);
                    }
                    vm->sql_param_count = 0;
                } else {
                    Context* ctx = vm->context;
                    if (ctx == NULL || ctx->pager == NULL) {
                        value_release(value_cursor(cursor));
                        set_runtime_error_sql(vm, "No database context");
                        THROW(vm);
                    }
                    cursor->result_handle = sql_exec(query_value.as.as_string, ctx);
                    for (int i = 0; i < vm->sql_param_count; i++) {
                        value_release(vm->sql_params[i]);
                    }
                    vm->sql_param_count = 0;
                    if (cursor->result_handle == NULL) {
                        value_release(value_cursor(cursor));
                        set_runtime_error_sql(vm, "SQL query failed");
                        THROW(vm);
                    }
                }
                cursor->is_open = 1;
                cursor->row_count = 0;
                cursor->found = 0;
                cursor->row_handle = NULL;
                if (!push(vm, value_cursor(cursor))) {
                    value_release(value_cursor(cursor));
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_CURSOR_FETCH: {
                if (vm->ip + 1 > end) return INTERPRET_RUNTIME_ERROR;
                uint8_t into_count = *vm->ip++;
                if (vm->ip + into_count > end) return INTERPRET_RUNTIME_ERROR;
                uint8_t into_slots[64];
                for (int i = 0; i < into_count; i++) {
                    into_slots[i] = *vm->ip++;
                }
                Value cursor_value;
                if (!pop(vm, &cursor_value)) return INTERPRET_RUNTIME_ERROR;
                if (cursor_value.type != VAL_CURSOR || cursor_value.as.as_cursor == NULL) {
                    value_release(cursor_value);
                    set_runtime_error(vm, "FETCH requires a cursor variable");
                    THROW(vm);
                }
                CursorObj* cursor = cursor_value.as.as_cursor;
                if (cursor == NULL || !cursor->is_open) {
                    value_release(cursor_value);
                    set_runtime_error(vm, "FETCH on closed cursor");
                    THROW(vm);
                }
                int has_row = 0;
                if (vm->driver != NULL) {
                    void* next = NULL;
                    has_row = vm->driver->result_next(vm->driver, cursor->result_handle, &next);
                    cursor->row_handle = has_row ? next : NULL;
                } else {
                    Row* next = result_next((Result*)cursor->result_handle);
                    has_row = next != NULL;
                    cursor->row_handle = next;
                }
                cursor->found = has_row;
                if (has_row) {
                    cursor->row_count++;
                    for (int i = 0; i < into_count; i++) {
                        Value col_value;
                        if (vm->driver != NULL) {
                            if (!vm->driver->row_get_column(vm->driver, cursor->row_handle, i, &col_value)) {
                                value_release(cursor_value);
                                set_runtime_error_from_driver_sql(vm, "Column access failed");
                                THROW(vm);
                            }
                        } else {
                            Row* row = (Row*)cursor->row_handle;
                            if (row == NULL || i >= row->field_count) {
                                value_release(cursor_value);
                                set_runtime_error_sql(vm, "Invalid column index");
                                THROW(vm);
                            }
                            Cell cell = row->fields[i].value;
                            switch (cell.type) {
                                case VAL_INT:    col_value = value_int(cell.as.as_int);       break;
                                case VAL_FLOAT:  col_value = value_float(cell.as.as_float);   break;
                                case VAL_STRING: col_value = value_string(strdup(cell.as.as_string)); break;
                                default:         col_value = value_int(0);                    break;
                            }
                        }
                        int slot = into_slots[i];
                        int depth = (int)(vm->stack_top - vm->frame_base);
                        if (slot >= depth) {
                            value_release(col_value);
                            value_release(cursor_value);
                            set_runtime_error(vm, "Invalid local variable slot");
                            THROW(vm);
                        }
                        value_retain(col_value);
                        value_release(vm->frame_base[slot]);
                        vm->frame_base[slot] = col_value;
                    }
                }
                value_release(cursor_value);
                break;
            }
            case OP_CURSOR_CLOSE: {
                Value cursor_value;
                if (!pop(vm, &cursor_value)) return INTERPRET_RUNTIME_ERROR;
                if (cursor_value.type != VAL_CURSOR || cursor_value.as.as_cursor == NULL) {
                    value_release(cursor_value);
                    set_runtime_error(vm, "CLOSE requires a cursor variable");
                    THROW(vm);
                }
                CursorObj* cursor = cursor_value.as.as_cursor;
                if (cursor == NULL) {
                    value_release(cursor_value);
                    break;
                }
                cursor->is_open = 0;
                cursor->found = 0;
                if (cursor->result_handle != NULL) {
                    if (cursor->driver != NULL) {
                        cursor->driver->result_free(cursor->driver, cursor->result_handle);
                    } else {
                        result_free((Result*)cursor->result_handle);
                    }
                    cursor->result_handle = NULL;
                }
                cursor->row_handle = NULL;
                value_release(cursor_value);
                break;
            }
            case OP_CURSOR_ATTR: {
                if (vm->ip + 2 > end) return INTERPRET_RUNTIME_ERROR;
                uint16_t idx = read_u16(vm->ip);
                vm->ip += 2;
                Value cursor_value;
                if (!pop(vm, &cursor_value)) return INTERPRET_RUNTIME_ERROR;
                if (cursor_value.type != VAL_CURSOR) {
                    value_release(cursor_value);
                    set_runtime_error(vm, "Cursor attribute requires a cursor variable");
                    THROW(vm);
                }
                CursorObj* cursor = cursor_value.as.as_cursor;
                if (idx >= (uint16_t)vm->chunk->constants_count) {
                    value_release(cursor_value);
                    return INTERPRET_RUNTIME_ERROR;
                }
                Value attr_value = vm->chunk->constants[idx];
                if (attr_value.type != VAL_STRING || attr_value.as.as_string == NULL) {
                    value_release(cursor_value);
                    set_runtime_error(vm, "Invalid cursor attribute");
                    THROW(vm);
                }
                const char* attr = attr_value.as.as_string;
                Value result = value_int(0);
                if (strcmp(attr, "found") == 0) {
                    result = value_bool(cursor != NULL ? cursor->found : 0);
                } else if (strcmp(attr, "notfound") == 0) {
                    result = value_bool(cursor != NULL ? !cursor->found : 1);
                } else if (strcmp(attr, "rowcount") == 0) {
                    result = value_int(cursor != NULL ? cursor->row_count : 0);
                } else if (strcmp(attr, "isopen") == 0) {
                    result = value_bool(cursor != NULL ? cursor->is_open : 0);
                } else {
                    value_release(cursor_value);
                    set_runtime_error(vm, "Unknown cursor attribute");
                    THROW(vm);
                }
                if (!push(vm, result)) {
                    value_release(cursor_value);
                    return INTERPRET_RUNTIME_ERROR;
                }
                value_release(cursor_value);
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
                    THROW(vm);
                }
                Value row_value;
                if (!pop(vm, &row_value)) return INTERPRET_RUNTIME_ERROR;
                if (row_value.type != VAL_ROW || row_value.as.as_row_handle == NULL) {
                    value_release(row_value);
                    set_runtime_error(vm, "Cannot access field on non-row value");
                    THROW(vm);
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
                        THROW(vm);
                    }
                } else {
                    Row* row = (Row*)vm->row_handle;
                    if (row == NULL || idx >= (uint16_t)row->field_count) {
                        set_runtime_error_sql(vm, "Invalid column index");
                        THROW(vm);
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
                    THROW(vm);
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
                            THROW(vm);
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
                            THROW(vm);
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
            case OP_STRUCT_BUILD: {
                if (vm->ip + 2 > end) return INTERPRET_RUNTIME_ERROR;
                uint16_t idx = read_u16(vm->ip);
                vm->ip += 2;
                if (idx >= (uint16_t)vm->chunk->constants_count) return INTERPRET_RUNTIME_ERROR;
                Value schema_value = vm->chunk->constants[idx];
                if (schema_value.type != VAL_STRING || schema_value.as.as_string == NULL) {
                    set_runtime_error(vm, "Invalid struct schema");
                    THROW(vm);
                }
                const char* schema = schema_value.as.as_string;
                int field_count = 0;
                for (const char* p = schema; *p != '\0'; p++) {
                    if (*p == ',') field_count++;
                }
                if (field_count > 0) field_count++;
                else if (schema[0] != '\0') field_count = 1;

                RowObj* row = row_obj_new(field_count);
                if (row == NULL) {
                    set_runtime_error(vm, "out of memory");
                    THROW(vm);
                }

                Value* fields = malloc(sizeof(Value) * (size_t)(field_count > 0 ? field_count : 1));
                if (fields == NULL && field_count > 0) {
                    row_obj_free(row);
                    set_runtime_error(vm, "out of memory");
                    THROW(vm);
                }
                for (int i = field_count - 1; i >= 0; i--) {
                    if (!pop(vm, &fields[i])) {
                        for (int j = i + 1; j < field_count; j++) value_release(fields[j]);
                        free(fields);
                        row_obj_free(row);
                        return INTERPRET_RUNTIME_ERROR;
                    }
                }

                const char* p = schema;
                int i = 0;
                while (*p != '\0') {
                    const char* start = p;
                    while (*p != '\0' && *p != ',') p++;
                    size_t len = (size_t)(p - start);
                    char* name = malloc(len + 1);
                    if (name == NULL) {
                        for (int j = 0; j < field_count; j++) value_release(fields[j]);
                        free(fields);
                        row_obj_free(row);
                        set_runtime_error(vm, "out of memory");
                        THROW(vm);
                    }
                    memcpy(name, start, len);
                    name[len] = '\0';
                    row_obj_set_column(row, i, name, fields[i]);
                    free(name);
                    i++;
                    if (*p == ',') p++;
                }

                free(fields);
                Value result = value_row(row);
                if (!push(vm, result)) {
                    value_release(result);
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
                THROW(vm);
                break;
            }
            case OP_RAISE: {
                if (vm->ip + 4 > end) return INTERPRET_RUNTIME_ERROR;
                uint16_t msg_idx = read_u16(vm->ip);
                vm->ip += 2;
                int16_t code = (int16_t)read_u16(vm->ip);
                vm->ip += 2;
                if (msg_idx >= (uint16_t)vm->chunk->constants_count) return INTERPRET_RUNTIME_ERROR;
                Value msg_value = vm->chunk->constants[msg_idx];
                if (msg_value.type != VAL_STRING || msg_value.as.as_string == NULL) {
                    set_runtime_error_ex(vm, "Runtime error", code);
                } else {
                    set_runtime_error_ex(vm, msg_value.as.as_string, code);
                }
                THROW(vm);
                break;
            }
            case OP_TRY: {
                if (vm->ip + 3 > end) return INTERPRET_RUNTIME_ERROR;
                uint16_t offset = read_u16(vm->ip);
                vm->ip += 2;
                uint8_t local_count = *vm->ip++;
                if (vm->try_count >= TRY_MAX) {
                    set_runtime_error(vm, "Too many nested try blocks");
                    THROW(vm);
                }
                /* Push a placeholder for the catch variable so its slot exists
                 * on the stack throughout the try block. vm_catch will replace it
                 * with the real error message; OP_END_TRY will pop it on the
                 * non-throwing path. */
                if (!push(vm, value_int(0))) {
                    set_runtime_error(vm, "Stack overflow");
                    THROW(vm);
                }
                TryFrame* tf = &vm->try_frames[vm->try_count++];
                tf->catch_ip = vm->ip + (int16_t)offset;
                tf->frame_count = vm->frame_count;
                tf->frame_base = vm->frame_base;
                tf->local_count = (int)local_count;
                tf->stack_top = vm->stack_top;
                break;
            }
            case OP_END_TRY: {
                if (vm->ip + 2 > end) return INTERPRET_RUNTIME_ERROR;
                int16_t offset = (int16_t)read_u16(vm->ip);
                vm->ip += 2;
                if (vm->try_count <= 0) {
                    set_runtime_error(vm, "END_TRY without try");
                    THROW(vm);
                }
                vm->try_count--;
                /* Pop the catch-variable placeholder that OP_TRY pushed. */
                Value placeholder;
                if (!pop(vm, &placeholder)) {
                    set_runtime_error(vm, "Stack underflow");
                    THROW(vm);
                }
                value_release(placeholder);
                uint8_t* target = vm->ip + offset;
                if (target < vm->chunk->code || target > end) {
                    set_runtime_error(vm, "Invalid jump target");
                    THROW(vm);
                }
                vm->ip = target;
                break;
            }
            default:
                set_runtime_error(vm, "Unknown opcode");
                THROW(vm);
        }
    }
}

static void vm_free_child(VM* vm) {
    if (vm == NULL) return;
    for (Value* p = vm->stack; p < vm->stack_top; p++) {
        value_release(*p);
    }
    for (int i = 0; i < vm->repl_local_count; i++) {
        value_release(vm->repl_locals[i]);
    }
    for (int i = 0; i < vm->global_count; i++) {
        value_release(vm->globals[i]);
    }
    if (vm->driver != NULL && vm->result_handle != NULL) {
        vm->driver->result_free(vm->driver, vm->result_handle);
    } else if (vm->driver == NULL) {
        result_free((Result*)vm->result_handle);
    }
    free(vm);
}

static int vm_call_autonomous(VM* parent, uint16_t target, uint8_t arg_count,
                              int out_count, int* out_positions, int* out_slots,
                              Value* out_result) {
    if (parent->driver == NULL || !parent->driver->is_sqlite) {
        return -1; /* fall back to normal call path */
    }
    VM* child = vm_init();
    if (child == NULL) {
        set_runtime_error(parent, "Out of memory");
        return 0;
    }
    DBDriver auto_driver;
    parent->driver->init(&auto_driver);
    if (!auto_driver.open(&auto_driver, parent->driver->connection_string)) {
        snprintf(parent->error_message, sizeof(parent->error_message), "%s", auto_driver.error_message);
        vm_free_child(child);
        return 0;
    }
    if (!auto_driver.begin(&auto_driver)) {
        set_runtime_error_from_driver(parent, "Autonomous BEGIN failed");
        auto_driver.close(&auto_driver);
        vm_free_child(child);
        return 0;
    }
    child->global_count = parent->global_count;
    for (int i = 0; i < parent->global_count; i++) {
        child->globals[i] = parent->globals[i];
        value_retain(child->globals[i]);
    }
    for (int i = 0; i < arg_count; i++) {
        Value arg = parent->stack_top[-arg_count + i];
        value_retain(arg);
        if (!push(child, arg)) {
            value_release(arg);
            set_runtime_error(parent, "Stack overflow");
            auto_driver.close(&auto_driver);
            vm_free_child(child);
            return 0;
        }
    }
    child->chunk = parent->chunk;
    child->ip = child->chunk->code + target;
    child->frame_base = child->stack;
    child->frame_count = 0;
    child->driver = &auto_driver;
    child->context = parent->context;
    InterpretResult run_result = vm_run(child, child->chunk->code + child->chunk->count);
    if (run_result == INTERPRET_OK) {
        Value result;
        if (!pop(child, &result)) {
            set_runtime_error(parent, "Autonomous call returned no value");
            auto_driver.close(&auto_driver);
            vm_free_child(child);
            return 0;
        }
        if (!auto_driver.commit(&auto_driver)) {
            set_runtime_error_from_driver(parent, "Autonomous commit failed");
            auto_driver.close(&auto_driver);
            vm_free_child(child);
            return 0;
        }
        for (int i = 0; i < out_count; i++) {
            int pos = out_positions[i];
            int slot = out_slots[i];
            if (pos < 0 || pos >= arg_count) {
                value_release(result);
                auto_driver.close(&auto_driver);
                vm_free_child(child);
                set_runtime_error(parent, "Invalid OUT parameter position");
                return 0;
            }
            if (slot < 0 || slot >= (parent->stack_top - parent->frame_base - arg_count)) {
                value_release(result);
                auto_driver.close(&auto_driver);
                vm_free_child(child);
                set_runtime_error(parent, "Invalid OUT parameter slot");
                return 0;
            }
            value_release(parent->frame_base[slot]);
            parent->frame_base[slot] = child->frame_base[pos];
            value_retain(parent->frame_base[slot]);
        }
        *out_result = result;
        auto_driver.close(&auto_driver);
        vm_free_child(child);
        return 1;
    }
    snprintf(parent->error_message, sizeof(parent->error_message), "%s", child->error_message);
    auto_driver.rollback(&auto_driver);
    auto_driver.close(&auto_driver);
    vm_free_child(child);
    return 0;
}

InterpretResult vm_interpret(VM* vm, Chunk* chunk) {
    vm->chunk = chunk;
    vm->ip = chunk->code;
    vm->error_message[0] = '\0';
    vm->sql_rowcount = 0;
    vm->try_count = 0;
    vm->local_count = 0;
    vm->repl_local_count = 0;
    return vm_run(vm, chunk->code + chunk->count);
}
