#ifndef MYDB_SQL_ENGINE_H
#define MYDB_SQL_ENGINE_H

#include "mydb.h"

struct Context {
    const char* db_path;
};

typedef struct {
    const char* name;
    int value;
} MockField;

typedef struct {
    MockField* fields;
    int field_count;
} MockRow;

struct Result {
    int row_count;
    int current;
    struct Row* rows;
};

struct Row {
    MockField* fields;
    int field_count;
};

typedef struct Cursor Cursor;
typedef struct Pager Pager;
typedef struct BTree BTree;

void    sql_engine_set_mock(const MockRow* rows, int row_count);
Result* sql_exec(const char* query, Context* ctx);
Row*    result_next(Result* res);
void    result_free(Result* res);
int     row_get_field(Row* row, const char* name);

Pager* pager_open(const char* filename);
void   pager_close(Pager* pager);

BTree* btree_create(Pager* pager);
void   btree_destroy(BTree* tree);

int os_open(const char* path);
int os_close(int fd);

#endif
