#ifndef MYDB_VM_H
#define MYDB_VM_H

#include "compiler.h"

#define STACK_MAX 256

typedef struct VM VM;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

VM*             vm_init(void);
void            vm_free(VM* vm);
InterpretResult vm_interpret(VM* vm, Chunk* chunk);
Value           vm_pop(VM* vm);

#endif
