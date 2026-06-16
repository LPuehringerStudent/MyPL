#ifndef MYDB_H
#define MYDB_H

typedef struct Context Context;
typedef struct Result Result;
typedef struct Row Row;

Result* sql_exec(const char* query, Context* ctx);
Row*    result_next(Result* res);

#endif
