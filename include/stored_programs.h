#ifndef MYDB_STORED_PROGRAMS_H
#define MYDB_STORED_PROGRAMS_H

#include "sql_engine.h"

/* Load all persisted top-level proc/func/trigger source code as a single
   string. Returns NULL if no program units are persisted or on error.
   Caller must free the returned string. */
char* stored_programs_load_source(DBDriver* driver, Context* ctx);

/* Persist top-level proc/func/trigger declarations from the given source
   string. Returns 1 on success, 0 on failure. */
int stored_programs_save_source(DBDriver* driver, Context* ctx, const char* source);

/* Remove one persisted unit (e.g. type "TRIGGER") by name, so a dropped
   trigger stays gone after a restart. Returns 1 on success (including
   "nothing stored"), 0 on failure. */
int stored_programs_drop_unit(DBDriver* driver, Context* ctx, const char* name, const char* type);

/* Return a copy of the loaded program source with any units that are
   redefined in `source` removed. The caller must free the result. */
char* stored_programs_filter_redefined(const char* loaded, const char* source);

#endif
