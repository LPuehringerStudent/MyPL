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
    OP_NEGATE,
    OP_NOT,
    OP_POP,
    OP_JZ,
    OP_JMP,
    OP_SQL,
    OP_SQL_NEXT,
    OP_GET_FIELD,
    OP_CALL,
    OP_RETURN
} OpCode;

typedef enum {
    VAL_INT,
    VAL_FLOAT,
    VAL_STRING
} ValueType;

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

void   write_chunk_u16(Chunk* chunk, uint16_t value);
uint16_t read_u16(const uint8_t* bytes);

Value value_int(int v);
Value value_float(double v);
Value value_string(char* s);
void value_print(Value value);

Value value_add(Value a, Value b);
Value value_sub(Value a, Value b);
Value value_mul(Value a, Value b);
Value value_div(Value a, Value b);
Value value_eq(Value a, Value b);
Value value_lt(Value a, Value b);
Value value_gt(Value a, Value b);
int   value_is_truthy(Value value);

int compile(const char* source, Chunk* chunk);

#endif
