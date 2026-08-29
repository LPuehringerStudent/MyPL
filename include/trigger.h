#ifndef MYDB_TRIGGER_H
#define MYDB_TRIGGER_H

#include <stddef.h>

/* Extract (event, table) from a DML/DDL statement for trigger firing.
 * The event is one of TRIGGER_INSERT/UPDATE/DELETE/CREATE/DROP (ast.h).
 * Returns 1 on success, 0 when the statement has no trigger-relevant shape. */
int sql_trigger_info(const char* sql, int* event, char* table, size_t table_size);

/* Case-insensitive identifier comparison (trigger/table names). */
int trigger_name_equals(const char* a, const char* b);

/* Parse `drop trigger <name>` (case-insensitive keywords, optional trailing
 * whitespace and semicolon). Returns 1 and copies the trigger name into
 * `name` on success, 0 when the statement is not a DROP TRIGGER. */
int sql_drop_trigger_name(const char* sql, char* name, size_t name_size);

#endif
