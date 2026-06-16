#include "sql_engine.h"

struct Pager {
    int fd;
};

Pager* pager_open(const char* filename) {
    (void)filename;
    return 0;
}

void pager_close(Pager* pager) {
    (void)pager;
}
