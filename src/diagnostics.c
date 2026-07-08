#include "diagnostics.h"

#include <stdio.h>

void format_error(char* buf, size_t size, const char* path, int line, int col,
                  const char* message) {
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
