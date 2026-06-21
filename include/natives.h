#ifndef MYDB_NATIVES_H
#define MYDB_NATIVES_H

#include "vm.h"

#define MAX_NATIVE_ARGS 8

typedef int (*NativeFn)(VM* vm, int argc, Value* argv, Value* out);

int native_count(void);
int native_find(const char* name);
int native_arity(int idx);
int native_call(VM* vm, int idx, int argc, Value* argv, Value* out);

#endif
