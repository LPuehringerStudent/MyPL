#include "diagnostics.h"

#include <stdio.h>

void format_error(char* buf, size_t size, const char* path, int line, int col,
                  const char* message) {
    if (line < 0) {
        /* Negative lines are the built-in and stored declarations the driver
           prepends to the user's source (see CompileOptions.line_offset). They
           have no position in the user's file, so say what they are instead
           of pointing at a line that is not there. */
        if (path != NULL && path[0] != '\0') {
            snprintf(buf, size, "%s: error: in built-in or stored declarations: %s", path, message);
        } else {
            snprintf(buf, size, "error: in built-in or stored declarations: %s", message);
        }
        return;
    }
    if (path != NULL && path[0] != '\0') {
        if (line > 0 && col > 0) {
            snprintf(buf, size, "%s:%d:%d: error: %s", path, line, col, message);
        } else if (line > 0) {
            snprintf(buf, size, "%s:%d: error: %s", path, line, message);
        } else {
            snprintf(buf, size, "%s: error: %s", path, message);
        }
    } else {
        if (line > 0 && col > 0) {
            snprintf(buf, size, "%d:%d: error: %s", line, col, message);
        } else if (line > 0) {
            snprintf(buf, size, "%d: error: %s", line, message);
        } else {
            snprintf(buf, size, "error: %s", message);
        }
    }
}
