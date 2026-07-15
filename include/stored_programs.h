#ifndef MYDB_STORED_PROGRAMS_H
#define MYDB_STORED_PROGRAMS_H

#include "sql_engine.h"

/* Load all persisted top-level proc/func source code as a single string.
   Returns NULL if no program units are persisted or on error.
   Caller must free the returned string. */
char* stored_programs_load_source(DBDriver* driver, Context* ctx);

/* Persist top-level proc/func declarations from the given source string.
   Returns 1 on success, 0 on failure. */
int stored_programs_save_source(DBDriver* driver, Context* ctx, const char* source);

/* Return a copy of the loaded program source with any units that are
   redefined in `source` removed. The caller must free the result. */
char* stored_programs_filter_redefined(const char* loaded, const char* source);

#endif
