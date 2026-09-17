#ifndef MYDB_PACKAGES_H
#define MYDB_PACKAGES_H

#include "sql_engine.h"

/* Load all persisted package source code as a single string.
   Returns NULL if no packages are persisted or on error.
   Caller must free the returned string. */
char* packages_load_source(DBDriver* driver, Context* ctx);

/* Load built-in package source code as a single string.
   Returns NULL if there are no built-in packages or on error.
   Caller must free the returned string. */
char* packages_load_builtins(void);

/* Return a copy of `loaded` without the package specs and bodies whose names
   are declared (as `package NAME` or `package body NAME`) in `source`, so a
   declaration in `source` replaces the loaded one. Returns NULL if `loaded`
   is NULL or nothing but whitespace remains. Caller must free the result. */
char* packages_filter_redefined(const char* loaded, const char* source);

/* Persist package source code from the given source string.
   If append is non-zero, the source is appended to existing persisted
   packages; otherwise it replaces them. Returns 1 on success, 0 on failure. */
int packages_save_source(DBDriver* driver, Context* ctx, const char* source, int append);

#endif
