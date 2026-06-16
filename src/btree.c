#include "sql_engine.h"

struct BTree {
    Pager* pager;
};

BTree* btree_create(Pager* pager) {
    (void)pager;
    return 0;
}

void btree_destroy(BTree* tree) {
    (void)tree;
}
