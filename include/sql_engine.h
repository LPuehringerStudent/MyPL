#ifndef MYDB_SQL_ENGINE_H
#define MYDB_SQL_ENGINE_H

#include <stddef.h>
#include "mydb.h"

struct Context {
    const char* db_path;
};

struct Result {
    int dummy;
};

struct Row {
    int dummy;
};

typedef struct Cursor Cursor;
typedef struct Pager Pager;
typedef struct BTree BTree;

Result* sql_exec(const char* query, Context* ctx);
Row*    result_next(Result* res);

Pager* pager_open(const char* filename);
void   pager_close(Pager* pager);

BTree* btree_create(Pager* pager);
void   btree_destroy(BTree* tree);

int os_open(const char* path);
int os_close(int fd);

#endif
