#ifndef MYDB_SQL_ENGINE_H
#define MYDB_SQL_ENGINE_H

#include <sys/types.h>

#include "mydb.h"
#include "compiler.h"

#define MAX_NAME_LEN 255

typedef struct {
    int type; /* VAL_INT, VAL_FLOAT, VAL_STRING */
    union {
        int    as_int;
        double as_float;
        char*  as_string;
    } as;
} Cell;

typedef struct {
    char* name;
    Cell  value;
} Field;

struct Row {
    Field* fields;
    int    field_count;
};

struct Result {
    int   row_count;
    int   current;
    Row*  rows;
};

typedef struct Column {
    char* name;
    int   type;
} Column;

typedef struct Table {
    char*    name;
    Column*  columns;
    int      column_count;
    int      first_row_page;
    int      last_row_page;
} Table;

struct Context {
    const char*    db_path;
    struct Pager*  pager;
};

/* Catalog lifecycle */
int    catalog_open(Context* ctx);
void   catalog_close(Context* ctx);
void   catalog_clear(Context* ctx);

/* Catalog DDL/DML */
Table* catalog_create_table(Context* ctx, const char* name, const char** columns, int* types, int column_count);
Table* catalog_find_table(Context* ctx, const char* name);
void   catalog_insert(Context* ctx, Table* table, Cell* cells);

/* Catalog introspection */
int         catalog_table_count(Context* ctx);
const char* catalog_table_name(Context* ctx, int index);
int         catalog_table_column_count(Context* ctx, int index);
const char* catalog_table_column_name(Context* ctx, int index, int col);
int         catalog_table_column_type(Context* ctx, int index, int col);

/* SQL execution */
Result* sql_exec(const char* query, Context* ctx);
int     sql_exec_ddl(const char* query, Context* ctx);
Row*    result_next(Result* res);
void    result_free(Result* res);
Cell    row_get_field(Row* row, const char* name);

/* Resolve the type of a column in a SELECT query. */
/* out_type receives one of VAL_INT, VAL_FLOAT, VAL_STRING on success. */
int sql_query_column_type(Context* ctx, const char* query, const char* column_name, int* out_type);

/* -------------------------------------------------------------------------- */
/* Database driver abstraction                                                */
/* -------------------------------------------------------------------------- */

typedef struct DBDriver DBDriver;

struct DBDriver {
    void* impl;
    char error_message[256];
    int (*open)(DBDriver* driver, const char* connection_string);
    void (*close)(DBDriver* driver);
    int (*exec)(DBDriver* driver, const char* sql, Value* params, int param_count);
    int (*query)(DBDriver* driver, const char* sql, Value* params, int param_count, void** result_handle);
    int (*result_next)(DBDriver* driver, void* result_handle, void** row_handle);
    int (*row_get_field)(DBDriver* driver, void* row_handle, const char* name, Value* out);
    int (*row_get_column)(DBDriver* driver, void* row_handle, int index, Value* out);
    int (*result_column_count)(DBDriver* driver, void* result_handle);
    const char* (*result_column_name)(DBDriver* driver, void* result_handle, int index);
    void (*result_free)(DBDriver* driver, void* result_handle);
    int (*begin)(DBDriver* driver);
    int (*commit)(DBDriver* driver);
    int (*rollback)(DBDriver* driver);
};

void custom_driver_init(DBDriver* driver);

/* Storage layer */
#define PAGE_SIZE 4096

typedef struct Cursor Cursor;
typedef struct Pager Pager;
typedef struct BTree BTree;

Pager* pager_open(const char* filename);
void   pager_close(Pager* pager);
int    pager_page_count(Pager* pager);
int    pager_allocate_page(Pager* pager);
void   pager_free_page(Pager* pager, int page_num);
void   pager_read_page(Pager* pager, int page_num, uint8_t* out);
void   pager_write_page(Pager* pager, int page_num, const uint8_t* data);

BTree* btree_create(Pager* pager);
void   btree_destroy(BTree* tree);

int    os_open(const char* path);
int    os_close(int fd);
int    os_read(int fd, void* buf, size_t count, off_t offset);
int    os_write(int fd, const void* buf, size_t count, off_t offset);
int    os_ftruncate(int fd, off_t length);

#endif
