#ifndef MYDB_VM_H
#define MYDB_VM_H

#include "compiler.h"
#include "sql_engine.h"

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
void            vm_set_driver(VM* vm, struct DBDriver* driver);
struct DBDriver* vm_get_driver(VM* vm);
void            vm_set_sql_rowcount(VM* vm, int rowcount);
InterpretResult vm_interpret(VM* vm, Chunk* chunk);
const char*     vm_get_error(VM* vm);
void            vm_set_error(VM* vm, const char* message);
void            vm_set_error_with_code(VM* vm, const char* message, int code);
int             vm_get_sql_rowcount(VM* vm);
int             vm_get_sql_found(VM* vm);
int             vm_get_sql_notfound(VM* vm);
int             vm_get_sql_code(VM* vm);
const char*     vm_get_sql_errm(VM* vm);
Value           vm_pop(VM* vm);
int             vm_stack_depth(VM* vm);

/* dbms_output buffer */
void            vm_dbms_output_enable(VM* vm, int limit);
void            vm_dbms_output_put_line(VM* vm, Value line);
void            vm_dbms_output_disable(VM* vm);
Value           vm_dbms_output_get_lines(VM* vm);

/* utl_file handles */
int             vm_utl_file_fopen(VM* vm, const char* path, const char* mode);
Value           vm_utl_file_get_line(VM* vm, int handle);
int             vm_utl_file_put_line(VM* vm, int handle, const char* text);
int             vm_utl_file_fclose(VM* vm, int handle);

/* dbms_sql helpers */
int             vm_dbms_sql_execute(VM* vm, const char* sql);
Value           vm_dbms_sql_query(VM* vm, const char* sql);

Value           vm_stack_get(VM* vm, int index);
int             vm_local_count(VM* vm);
Value           vm_local_get(VM* vm, int index);

#endif
