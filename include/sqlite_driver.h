#ifndef MYDB_SQLITE_DRIVER_H
#define MYDB_SQLITE_DRIVER_H

#include <sqlite3.h>
#include "sql_engine.h"

typedef struct {
    sqlite3* db;
} SQLiteImpl;

void sqlite_driver_init(DBDriver* driver);

#endif
