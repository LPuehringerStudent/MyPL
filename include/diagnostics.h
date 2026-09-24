#ifndef MYDB_DIAGNOSTICS_H
#define MYDB_DIAGNOSTICS_H

#include <stddef.h>

/* Format an error message with source location.
 *
 * Produces one of:
 *   <path>:<line>:<col>: error: <message>
 *   <path>:<line>: error: <message>      (when col <= 0)
 *   <line>:<col>: error: <message>       (when path is NULL/empty)
 *   <line>: error: <message>             (when only line is available)
 *   error: <message>                     (when no location is available)
 *
 * A negative line means the location is in the built-in or stored
 * declarations prepended to the user's source; the message then reads
 *   <path>: error: in built-in or stored declarations: <message>
 */
void format_error(char* buf, size_t size, const char* path, int line, int col,
                  const char* message);

#endif
