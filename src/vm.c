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

static void push(VM* vm, Value value) {
    *vm->stack_top = value;
    vm->stack_top++;
}

static Value pop(VM* vm) {
    vm->stack_top--;
    return *vm->stack_top;
}

static void binary_op(VM* vm, Value (*op)(Value, Value)) {
    Value b = pop(vm);
    Value a = pop(vm);
    push(vm, op(a, b));
}

Value vm_pop(VM* vm) {
    return pop(vm);
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
                push(vm, vm->chunk->constants[idx]);
                break;
            }
            case OP_GET_LOCAL: {
                uint8_t slot = *vm->ip++;
                push(vm, vm->stack[slot]);
                break;
            }
            case OP_SET_LOCAL: {
                uint8_t slot = *vm->ip++;
                vm->stack[slot] = *(vm->stack_top - 1);
                break;
            }
            case OP_ADD: {
                binary_op(vm, value_add);
                break;
            }
            case OP_SUB: {
                binary_op(vm, value_sub);
                break;
            }
            case OP_MUL: {
                binary_op(vm, value_mul);
                break;
            }
            case OP_DIV: {
                binary_op(vm, value_div);
                break;
            }
            case OP_EQ: {
                binary_op(vm, value_eq);
                break;
            }
            case OP_LT: {
                binary_op(vm, value_lt);
                break;
            }
            case OP_GT: {
                binary_op(vm, value_gt);
                break;
            }
            case OP_RETURN: {
                return INTERPRET_OK;
            }
        }
    }
}
