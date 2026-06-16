#ifndef MYDB_COMPILER_H
#define MYDB_COMPILER_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    OP_CONST,
    OP_GET_LOCAL,
    OP_SET_LOCAL,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_EQ,
    OP_LT,
    OP_GT,
    OP_JZ,
    OP_JMP,
    OP_SQL,
    OP_SQL_NEXT,
    OP_GET_FIELD,
    OP_CALL,
    OP_RETURN
} OpCode;

typedef struct {
    int type;
    union {
        int    as_int;
        double as_float;
        char*  as_string;
        void*  as_row_handle;
    } as;
} Value;

typedef struct {
    uint8_t* code;
    int      count;
    int      capacity;

    Value* constants;
    int    constants_count;
    int    constants_capacity;
} Chunk;

void init_chunk(Chunk* chunk);
void free_chunk(Chunk* chunk);
void write_chunk(Chunk* chunk, uint8_t byte);
int  add_constant(Chunk* chunk, Value value);

Value value_int(int v);
Value value_float(double v);
Value value_string(char* s);

int compile(const char* source, Chunk* chunk);

#endif
