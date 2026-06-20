#ifndef MYDB_VM_H
#define MYDB_VM_H

#include "compiler.h"

struct Context;

#define STACK_MAX 256

typedef struct VM VM;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

VM*             vm_init(void);
void            vm_free(VM* vm);
void            vm_set_context(VM* vm, struct Context* ctx);
InterpretResult vm_interpret(VM* vm, Chunk* chunk);
const char*     vm_get_error(VM* vm);
Value           vm_pop(VM* vm);

#endif
