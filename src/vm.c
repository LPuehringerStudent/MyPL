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
            case OP_RETURN: {
                return INTERPRET_OK;
            }
        }
    }
}
