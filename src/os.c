#include "sql_engine.h"

int os_open(const char* path) {
    (void)path;
    return -1;
}

int os_close(int fd) {
    (void)fd;
    return -1;
}
