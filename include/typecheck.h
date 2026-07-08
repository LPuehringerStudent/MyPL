#ifndef MYDB_TYPECHECK_H
#define MYDB_TYPECHECK_H

#include "ast.h"

struct Context;

typedef struct ProcSignature ProcSignature;
struct ProcSignature {
    const char* name;
    Type* return_type;
    Type** param_types;
    int param_count;
};

int typecheck_program(Program* program,
                      ProcSignature* procs,
                      int proc_count,
                      struct Context* ctx,
                      const char* source_path,
                      char* error,
                      size_t error_size);

#endif
