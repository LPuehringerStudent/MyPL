#ifndef MYDB_PACKAGES_H
#define MYDB_PACKAGES_H

#include "sql_engine.h"

/* Load all persisted package source code as a single string.
   Returns NULL if no packages are persisted or on error.
   Caller must free the returned string. */
char* packages_load_source(DBDriver* driver, Context* ctx);

/* Persist package source code from the given source string.
   If append is non-zero, the source is appended to existing persisted
   packages; otherwise it replaces them. Returns 1 on success, 0 on failure. */
int packages_save_source(DBDriver* driver, Context* ctx, const char* source, int append);

#endif
