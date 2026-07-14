#ifndef MYDB_COMPILER_H
#define MYDB_COMPILER_H

#include <stddef.h>
#include <stdint.h>

typedef struct ArrayObj ArrayObj;
typedef struct MapObj MapObj;
typedef struct RowObj RowObj;
typedef struct CursorObj CursorObj;
typedef struct DBDriver DBDriver;

typedef enum {
    OBJ_STRING,
    OBJ_ARRAY,
    OBJ_MAP,
    OBJ_ROW,
    OBJ_CURSOR
} ObjType;

typedef struct {
    ObjType type;
    int ref_count;
} Obj;

struct CursorObj {
    Obj obj;
    DBDriver* driver;
    void* result_handle;
    void* row_handle;
    int is_open;
    int row_count;
    int found;
};

typedef enum {
    OP_CONST,
    OP_GET_LOCAL,
    OP_SET_LOCAL,
    OP_GET_GLOBAL,
    OP_SET_GLOBAL,
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
    OP_DUP,
    OP_JZ,
    OP_JMP,
    OP_SQL,
    OP_SQL_NEXT,
    OP_GET_FIELD,
    OP_CALL,
    OP_CALL_OUT,
    OP_RETURN,
    OP_PRINT,
    OP_ARRAY_BUILD,
    OP_INDEX_GET,
    OP_INDEX_SET,
    OP_NATIVE_CALL,
    OP_SQL_EXEC,
    OP_SQL_BIND_INT,
    OP_SQL_BIND_FLOAT,
    OP_SQL_BIND_STRING,
    OP_SQL_BEGIN,
    OP_SQL_COMMIT,
    OP_SQL_ROLLBACK,
    OP_ROW_GET,
    OP_SQL_GET_COLUMN,
    OP_SQL_TO_ARRAY,
    OP_STRUCT_BUILD,
    OP_MAP_BUILD,
    OP_TRY,
    OP_END_TRY,
    OP_RUNTIME_ERROR,
    OP_RAISE,
    OP_CURSOR_OPEN,
    OP_CURSOR_FETCH,
    OP_CURSOR_CLOSE,
    OP_CURSOR_ATTR
} OpCode;

typedef enum {
    VAL_INT,
    VAL_FLOAT,
    VAL_STRING,
    VAL_BOOL,
    VAL_ARRAY,
    VAL_MAP,
    VAL_ROW,
    VAL_CURSOR
} ValueType;

typedef struct {
    int type;
    union {
        int       as_int;
        double    as_float;
        char*     as_string;
        void*     as_row_handle;
        ArrayObj* as_array;
        MapObj*   as_map;
        CursorObj* as_cursor;
    } as;
} Value;

struct MapObj {
    Obj obj;
    Value* keys;
    Value* values;
    int count;
    int capacity;
};

typedef struct {
    uint8_t* code;
    int      count;
    int      capacity;

    int* lines;
    int  lines_count;
    int  lines_capacity;

    int* columns;
    int  columns_count;
    int  columns_capacity;

    Value* constants;
    int    constants_count;
    int    constants_capacity;

    const char* source_path;
} Chunk;

void init_chunk(Chunk* chunk);
void free_chunk(Chunk* chunk);
void write_chunk(Chunk* chunk, uint8_t byte);
void write_chunk_line(Chunk* chunk, uint8_t byte, int line, int column);
int  add_constant(Chunk* chunk, Value value);

void   write_chunk_u16(Chunk* chunk, uint16_t value);
void   write_chunk_u16_line(Chunk* chunk, uint16_t value, int line, int column);
uint16_t read_u16(const uint8_t* bytes);

Value value_int(int v);
Value value_float(double v);
Value value_string(char* s);
Value value_bool(int v);
Value value_array(ArrayObj* array);
Value value_map(MapObj* map);
Value value_row(RowObj* row);
Value value_cursor(CursorObj* cursor);
CursorObj* cursor_obj_new(DBDriver* driver);
void value_print(Value value);

void value_retain(Value v);
void value_release(Value v);
int  value_ref_count(Value v);

ArrayObj* array_new(void);
void      array_free(ArrayObj* array);
int       array_append(ArrayObj* array, Value value);
Value     array_get(ArrayObj* array, int index);
void      array_set(ArrayObj* array, int index, Value value);
int       array_length(ArrayObj* array);
int       array_extend(ArrayObj* array, int count);
int       array_trim(ArrayObj* array, int count);
void      array_pool_free_all(void);

MapObj*   map_new(void);
void      map_free(MapObj* map);
int       map_set(MapObj* map, Value key, Value value);
int       map_get(MapObj* map, Value key, Value* out);
int       map_delete(MapObj* map, Value key);
int       map_count(MapObj* map);
int       map_first_key(MapObj* map, Value* out);
int       map_last_key(MapObj* map, Value* out);
int       map_next_key(MapObj* map, Value key, Value* out);
int       map_prior_key(MapObj* map, Value key, Value* out);

RowObj* row_obj_new(int column_count);
void    row_obj_free(RowObj* row);
void    row_obj_set_column(RowObj* row, int index, const char* name, Value value);
Value   row_obj_get_field(RowObj* row, const char* name);
int     row_obj_set_field(RowObj* row, const char* name, Value value);

Value value_add(Value a, Value b);
Value value_sub(Value a, Value b);
Value value_mul(Value a, Value b);
Value value_div(Value a, Value b);
Value value_eq(Value a, Value b);
Value value_lt(Value a, Value b);
Value value_gt(Value a, Value b);
int   value_is_truthy(Value value);

struct Context;

int compile(const char* source, Chunk* chunk, char* error, size_t error_size);
int compile_with_context(const char* source, Chunk* chunk, char* error, size_t error_size, struct Context* ctx);
int compile_with_path(const char* source, Chunk* chunk, const char* path, char* error, size_t error_size);
int compile_with_context_and_path(const char* source, Chunk* chunk, const char* path, char* error, size_t error_size, struct Context* ctx);

#endif
