#ifndef MYDB_SQL_ENGINE_H
#define MYDB_SQL_ENGINE_H

#include "mydb.h"
#include "compiler.h"

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
    Row*     rows;
    int      row_count;
    int      row_capacity;
} Table;

struct Context {
    const char* db_path;
};

/* Catalog */
void   catalog_clear(void);
Table* catalog_create_table(const char* name, const char** columns, int* types, int column_count);
Table* catalog_find_table(const char* name);
void   catalog_insert(Table* table, Cell* cells);

/* Execution */
Result* sql_exec(const char* query, Context* ctx);
Row*    result_next(Result* res);
void    result_free(Result* res);
Cell    row_get_field(Row* row, const char* name);

/* Storage layer stubs */
typedef struct Cursor Cursor;
typedef struct Pager Pager;
typedef struct BTree BTree;

Pager* pager_open(const char* filename);
void   pager_close(Pager* pager);

BTree* btree_create(Pager* pager);
void   btree_destroy(BTree* tree);

int os_open(const char* path);
int os_close(int fd);

#endif
