#ifndef MYDB_NATIVES_H
#define MYDB_NATIVES_H

#include "vm.h"

#define MAX_NATIVE_ARGS 8

/* A native declared with this arity takes any number of arguments (up to
   MAX_NATIVE_ARGS) and checks them itself. */
#define NATIVE_VARIADIC (-1)

/* external_call_sig descriptors: "R(ARGS)", one letter per type, i = int,
   d = float (C double), s = string (const char*). */
#define EXTERNAL_SIG_MAX_ARGS 4
enum { EXTERNAL_SIG_INT, EXTERNAL_SIG_FLOAT, EXTERNAL_SIG_STRING };

/* Parses `sig` into its return type and argument types. Returns 0 and
   describes the problem in `error` when it is malformed. */
int external_sig_parse(const char* sig, int* ret, int* arg_types, int* arg_count,
                       char* error, size_t error_size);

typedef int (*NativeFn)(VM* vm, int argc, Value* argv, Value* out);

int native_count(void);
int native_find(const char* name);
int native_arity(int idx);
int native_call(VM* vm, int idx, int argc, Value* argv, Value* out);

#endif
