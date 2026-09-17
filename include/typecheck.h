#ifndef MYDB_TYPECHECK_H
#define MYDB_TYPECHECK_H

#include "ast.h"

struct Context;

typedef struct ProcSignature ProcSignature;
struct ProcSignature {
    const char* name;
    Type* return_type;
    Type** param_types;
    ParamMode* param_modes;
    int param_count;
    /* Set for a package member declared in its package's spec (interface);
       clear for a body-only helper. Meaningless outside package members. */
    int is_public;
};

int typecheck_program(Program* program,
                      ProcSignature* procs,
                      int proc_count,
                      struct Context* ctx,
                      const char* source_path,
                      char* error,
                      size_t error_size,
                      const char* const* main_seed_names,
                      Type* const* main_seed_types,
                      int main_seed_count);

#endif
