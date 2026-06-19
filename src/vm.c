#include <stdlib.h>
#include <string.h>

#include "vm.h"
#include "sql_engine.h"

struct VM {
    Chunk*   chunk;
    uint8_t* ip;
    Value    stack[STACK_MAX];
    Value*   stack_top;
    Value*   frames[STACK_MAX];
    uint8_t* return_ips[STACK_MAX];
    int      frame_count;
    Value*   frame_base;
    Result*  result;
    Row*     row;
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
    return vm;
}

void vm_free(VM* vm) {
    if (vm == NULL) return;
    result_free(vm->result);
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
    return push(vm, op(a, b));
}

Value vm_pop(VM* vm) {
    Value value;
    pop(vm, &value);
    return value;
}

InterpretResult vm_interpret(VM* vm, Chunk* chunk) {
    vm->chunk = chunk;
    vm->ip = chunk->code;
    uint8_t* end = chunk->code + chunk->count;

    for (;;) {
        if (vm->ip >= end) {
            return INTERPRET_RUNTIME_ERROR;
        }
        uint8_t op = *vm->ip++;
        switch (op) {
            case OP_CONST: {
                if (vm->ip + 2 > end) return INTERPRET_RUNTIME_ERROR;
                uint16_t idx = read_u16(vm->ip);
                vm->ip += 2;
                if (idx >= (uint16_t)vm->chunk->constants_count) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                if (!push(vm, vm->chunk->constants[idx])) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_GET_LOCAL: {
                if (vm->ip + 1 > end) return INTERPRET_RUNTIME_ERROR;
                uint8_t slot = *vm->ip++;
                int depth = (int)(vm->stack_top - vm->frame_base);
                if (slot >= depth) return INTERPRET_RUNTIME_ERROR;
                if (!push(vm, vm->frame_base[slot])) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SET_LOCAL: {
                if (vm->ip + 1 > end) return INTERPRET_RUNTIME_ERROR;
                uint8_t slot = *vm->ip++;
                int depth = (int)(vm->stack_top - vm->frame_base);
                if (slot >= depth) return INTERPRET_RUNTIME_ERROR;
                vm->frame_base[slot] = *(vm->stack_top - 1);
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
                if (value.type == VAL_FLOAT) {
                    if (!push(vm, value_float(-value.as.as_float))) return INTERPRET_RUNTIME_ERROR;
                } else if (value.type == VAL_INT) {
                    if (!push(vm, value_int(-value.as.as_int))) return INTERPRET_RUNTIME_ERROR;
                } else {
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_NOT: {
                Value value;
                if (!pop(vm, &value)) return INTERPRET_RUNTIME_ERROR;
                if (!push(vm, value_int(!value_is_truthy(value)))) return INTERPRET_RUNTIME_ERROR;
                break;
            }
            case OP_SQL: {
                if (vm->ip + 2 > end) return INTERPRET_RUNTIME_ERROR;
                uint16_t idx = read_u16(vm->ip);
                vm->ip += 2;
                if (idx >= (uint16_t)vm->chunk->constants_count) return INTERPRET_RUNTIME_ERROR;
                Value query_value = vm->chunk->constants[idx];
                if (query_value.type != VAL_STRING || query_value.as.as_string == NULL) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                result_free(vm->result);
                vm->result = sql_exec(query_value.as.as_string, NULL);
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
                    return INTERPRET_RUNTIME_ERROR;
                }
                int field_value = row_get_field(vm->row, name_value.as.as_string);
                if (!push(vm, value_int(field_value))) return INTERPRET_RUNTIME_ERROR;
                break;
            }
            case OP_JZ: {
                if (vm->ip + 2 > end) return INTERPRET_RUNTIME_ERROR;
                int16_t offset = (int16_t)read_u16(vm->ip);
                vm->ip += 2;
                Value cond;
                if (!pop(vm, &cond)) return INTERPRET_RUNTIME_ERROR;
                if (!value_is_truthy(cond)) {
                    uint8_t* target = vm->ip + offset;
                    if (target < vm->chunk->code || target > end) {
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
                Value result;
                if (!pop(vm, &result)) return INTERPRET_RUNTIME_ERROR;
                vm->frame_count--;
                vm->frame_base = vm->frames[vm->frame_count];
                vm->ip = vm->return_ips[vm->frame_count];
                vm->stack_top = vm->frame_base;
                if (!push(vm, result)) return INTERPRET_RUNTIME_ERROR;
                break;
            }
            default:
                return INTERPRET_RUNTIME_ERROR;
        }
    }
}
