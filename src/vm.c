#include <stdlib.h>

#include "vm.h"

struct VM {
    Chunk*   chunk;
    uint8_t* ip;
    Value    stack[STACK_MAX];
    Value*   stack_top;
};

VM* vm_init(void) {
    VM* vm = malloc(sizeof(VM));
    if (vm == NULL) return NULL;
    vm->chunk = NULL;
    vm->ip = NULL;
    vm->stack_top = vm->stack;
    return vm;
}

void vm_free(VM* vm) {
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

    for (;;) {
        uint8_t op = *vm->ip++;
        switch (op) {
            case OP_CONST: {
                uint16_t idx = read_u16(vm->ip);
                vm->ip += 2;
                if (!push(vm, vm->chunk->constants[idx])) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_GET_LOCAL: {
                uint8_t slot = *vm->ip++;
                if (!push(vm, vm->stack[slot])) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SET_LOCAL: {
                uint8_t slot = *vm->ip++;
                vm->stack[slot] = *(vm->stack_top - 1);
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
            case OP_JZ: {
                uint16_t offset = read_u16(vm->ip);
                vm->ip += 2;
                Value cond;
                if (!pop(vm, &cond)) return INTERPRET_RUNTIME_ERROR;
                if (!value_is_truthy(cond)) {
                    vm->ip += offset;
                }
                break;
            }
            case OP_JMP: {
                uint16_t offset = read_u16(vm->ip);
                vm->ip += 2;
                vm->ip += offset;
                break;
            }
            case OP_RETURN: {
                return INTERPRET_OK;
            }
        }
    }
}
