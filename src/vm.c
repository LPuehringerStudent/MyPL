#include "vm.h"

struct VM {
    Chunk* chunk;
};

VM* vm_init(void) {
    return 0;
}

void vm_free(VM* vm) {
    (void)vm;
}

InterpretResult vm_interpret(VM* vm, Chunk* chunk) {
    (void)vm;
    (void)chunk;
    return INTERPRET_OK;
}
