#include "test_harness.h"
#include "sql_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* B-tree unit tests. The tree maps a Cell key to a row locator
   (row_page, row_offset); tests use synthetic locators. */

static char* make_temp_path(void) {
    char* path = malloc(256);
    if (path == NULL) return NULL;
    snprintf(path, 256, "/tmp/mydb_test_btree_%d.db", (int)getpid());
    unlink(path);
    return path;
}

static void cleanup(const char* path) {
    unlink(path);
    free((void*)path);
}

static Cell int_key(int v) {
    Cell c;
    c.type = VAL_INT;
    c.as.as_int = v;
    return c;
}

static Cell float_key(double v) {
    Cell c;
    c.type = VAL_FLOAT;
    c.as.as_float = v;
    return c;
}

static Cell string_key(const char* s) {
    Cell c;
    c.type = VAL_STRING;
    c.as.as_string = (char*)s;
    return c;
}

typedef struct {
    int pages[2048];
    int offsets[2048];
    int count;
} ScanLog;

static void scan_log_fn(int row_page, int row_offset, void* user) {
    ScanLog* log = (ScanLog*)user;
    if (log->count >= 2048) return;
    log->pages[log->count] = row_page;
    log->offsets[log->count] = row_offset;
    log->count++;
}

static int log_has(ScanLog* log, int row_page, int row_offset) {
    for (int i = 0; i < log->count; i++) {
        if (log->pages[i] == row_page && log->offsets[i] == row_offset) return 1;
    }
    return 0;
}

TEST(btree_insert_and_search_small) {
    char* path = make_temp_path();
    Pager* pager = pager_open(path);
    ASSERT_PTR_NOT_NULL(pager);
    BTree* tree = btree_create(pager);
    ASSERT_PTR_NOT_NULL(tree);

    for (int i = 0; i < 10; i++) {
        Cell k = int_key(i * 10);
        ASSERT_INT_EQ(1, btree_insert(tree, &k, 100 + i, 8 + i));
    }

    for (int i = 0; i < 10; i++) {
        Cell k = int_key(i * 10);
        ScanLog log = {0};
        int n = btree_scan_eq(tree, &k, scan_log_fn, &log);
        ASSERT_INT_EQ(1, n);
        ASSERT_INT_EQ(1, log_has(&log, 100 + i, 8 + i));
    }

    Cell missing = int_key(15);
    ScanLog log = {0};
    ASSERT_INT_EQ(0, btree_scan_eq(tree, &missing, scan_log_fn, &log));

    btree_destroy(tree);
    pager_close(pager);
    cleanup(path);
}

TEST(btree_persists_after_reopen) {
    char* path = make_temp_path();
    Pager* pager = pager_open(path);
    BTree* tree = btree_create(pager);
    Cell k = string_key("hello");
    ASSERT_INT_EQ(1, btree_insert(tree, &k, 7, 64));
    int root = btree_root_page(tree);
    ASSERT(root > 0);
    btree_destroy(tree);
    pager_close(pager);

    pager = pager_open(path);
    tree = btree_open(pager, root);
    ASSERT_PTR_NOT_NULL(tree);
    ScanLog log = {0};
    ASSERT_INT_EQ(1, btree_scan_eq(tree, &k, scan_log_fn, &log));
    ASSERT_INT_EQ(1, log_has(&log, 7, 64));

    btree_destroy(tree);
    pager_close(pager);
    cleanup(path);
}

TEST(btree_sequential_inserts_split_and_search) {
    char* path = make_temp_path();
    Pager* pager = pager_open(path);
    BTree* tree = btree_create(pager);
    ASSERT_PTR_NOT_NULL(tree);

    /* 600 keys forces several leaf splits and at least one root split. */
    for (int i = 0; i < 600; i++) {
        Cell k = int_key(i);
        ASSERT_INT_EQ(1, btree_insert(tree, &k, 1000 + i, i * 2));
    }

    for (int i = 0; i < 600; i++) {
        Cell k = int_key(i);
        ScanLog log = {0};
        int n = btree_scan_eq(tree, &k, scan_log_fn, &log);
        ASSERT_INT_EQ(1, n);
        ASSERT_INT_EQ(1, log_has(&log, 1000 + i, i * 2));
    }

    btree_destroy(tree);
    pager_close(pager);
    cleanup(path);
}

TEST(btree_reverse_and_random_inserts) {
    char* path = make_temp_path();
    Pager* pager = pager_open(path);
    BTree* tree = btree_create(pager);
    ASSERT_PTR_NOT_NULL(tree);

    for (int i = 400; i >= 0; i--) {
        Cell k = int_key(i);
        ASSERT_INT_EQ(1, btree_insert(tree, &k, i, i));
    }

    /* Deterministic pseudo-random inserts (may duplicate keys). */
    unsigned int state = 12345;
    for (int i = 0; i < 300; i++) {
        state = state * 1103515245u + 12345u;
        int v = (int)((state >> 16) % 500);
        Cell k = int_key(v);
        ASSERT_INT_EQ(1, btree_insert(tree, &k, 5000 + i, i));
    }

    for (int i = 0; i <= 400; i++) {
        Cell k = int_key(i);
        ScanLog log = {0};
        int n = btree_scan_eq(tree, &k, scan_log_fn, &log);
        ASSERT(n >= 1);
        ASSERT_INT_EQ(1, log_has(&log, i, i));
    }

    btree_destroy(tree);
    pager_close(pager);
    cleanup(path);
}

TEST(btree_string_keys_ordered_and_searchable) {
    char* path = make_temp_path();
    Pager* pager = pager_open(path);
    BTree* tree = btree_create(pager);
    ASSERT_PTR_NOT_NULL(tree);

    const char* words[] = {"pear", "apple", "kiwi", "banana", "plum", "fig"};
    int n_words = (int)(sizeof(words) / sizeof(words[0]));
    for (int i = 0; i < n_words; i++) {
        Cell k = string_key(words[i]);
        ASSERT_INT_EQ(1, btree_insert(tree, &k, i + 1, i));
    }

    for (int i = 0; i < n_words; i++) {
        Cell k = string_key(words[i]);
        ScanLog log = {0};
        ASSERT_INT_EQ(1, btree_scan_eq(tree, &k, scan_log_fn, &log));
        ASSERT_INT_EQ(1, log_has(&log, i + 1, i));
    }

    /* Range scan from 'banana' (inclusive) to 'pe' prefix end. */
    Cell lo = string_key("banana");
    Cell hi = string_key("pear");
    ScanLog log = {0};
    int n = btree_scan_range(tree, &lo, 1, &hi, 1, scan_log_fn, &log);
    /* expected: banana, fig, kiwi, pear */
    ASSERT_INT_EQ(4, n);
    ASSERT_INT_EQ(1, log_has(&log, 4, 3)); /* banana */
    ASSERT_INT_EQ(1, log_has(&log, 6, 5)); /* fig */
    ASSERT_INT_EQ(1, log_has(&log, 3, 2)); /* kiwi */
    ASSERT_INT_EQ(1, log_has(&log, 1, 0)); /* pear */

    btree_destroy(tree);
    pager_close(pager);
    cleanup(path);
}

/* Exactly BTREE_STRING_KEY_BYTES significant characters, so anything appended
   falls outside the key. */
#define SHARED_PREFIX "abcdefghijklmnopqrstuvwxyz0123456789"

/* Strings that differ only past the significant prefix are one key as far as
   the tree is concerned. The documented contract is that this errs towards
   more candidates, never fewer, so callers can re-check the predicate. */
TEST(btree_string_keys_sharing_the_prefix_over_approximate) {
    char* path = make_temp_path();
    Pager* pager = pager_open(path);
    BTree* tree = btree_create(pager);

    ASSERT_INT_EQ(BTREE_STRING_KEY_BYTES, (int)strlen(SHARED_PREFIX));

    Cell alpha = string_key(SHARED_PREFIX "-alpha");
    Cell beta = string_key(SHARED_PREFIX "-beta");
    ASSERT_INT_EQ(1, btree_insert(tree, &alpha, 1, 1));
    ASSERT_INT_EQ(1, btree_insert(tree, &beta, 2, 2));

    /* Either literal finds both rows: the surplus is the caller's to filter. */
    ScanLog log = {0};
    ASSERT_INT_EQ(2, btree_scan_eq(tree, &alpha, scan_log_fn, &log));
    ASSERT_INT_EQ(1, log_has(&log, 1, 1));
    ASSERT_INT_EQ(1, log_has(&log, 2, 2));

    ScanLog log2 = {0};
    ASSERT_INT_EQ(2, btree_scan_eq(tree, &beta, scan_log_fn, &log2));
    ASSERT_INT_EQ(1, log_has(&log2, 1, 1));
    ASSERT_INT_EQ(1, log_has(&log2, 2, 2));

    /* A difference inside the prefix is a distinct key, however long the
       string is. */
    Cell other = string_key("Xbcdefghijklmnopqrstuvwxyz0123456789-alpha");
    ASSERT_INT_EQ(1, btree_insert(tree, &other, 3, 3));
    ScanLog log3 = {0};
    ASSERT_INT_EQ(1, btree_scan_eq(tree, &other, scan_log_fn, &log3));
    ASSERT_INT_EQ(1, log_has(&log3, 3, 3));

    btree_destroy(tree);
    pager_close(pager);
    cleanup(path);
}

/* Why a range bound taken from a truncated literal has to be inclusive: the
   bound is only a prefix of what the caller asked for, so excluding it drops
   every row that shares the prefix. */
TEST(btree_string_range_bounds_from_truncated_literals_need_widening) {
    char* path = make_temp_path();
    Pager* pager = pager_open(path);
    BTree* tree = btree_create(pager);

    Cell alpha = string_key(SHARED_PREFIX "-alpha");
    Cell beta = string_key(SHARED_PREFIX "-beta");
    Cell below = string_key("aardvark");
    Cell above = string_key("zzz");
    ASSERT_INT_EQ(1, btree_insert(tree, &alpha, 1, 1));
    ASSERT_INT_EQ(1, btree_insert(tree, &beta, 2, 2));
    ASSERT_INT_EQ(1, btree_insert(tree, &below, 3, 3));
    ASSERT_INT_EQ(1, btree_insert(tree, &above, 4, 4));

    /* "name > SHARED_PREFIX-alpha" really matches -beta and zzz. An inclusive
       lower bound reaches all three candidates, -alpha included. */
    ScanLog inclusive = {0};
    ASSERT_INT_EQ(3, btree_scan_range(tree, &alpha, 1, NULL, 0,
                                      scan_log_fn, &inclusive));
    ASSERT_INT_EQ(1, log_has(&inclusive, 1, 1));
    ASSERT_INT_EQ(1, log_has(&inclusive, 2, 2));
    ASSERT_INT_EQ(1, log_has(&inclusive, 4, 4));

    /* Excluding it loses -beta, which does satisfy the predicate. */
    ScanLog exclusive = {0};
    ASSERT_INT_EQ(1, btree_scan_range(tree, &alpha, 0, NULL, 0,
                                      scan_log_fn, &exclusive));
    ASSERT_INT_EQ(0, log_has(&exclusive, 2, 2));

    /* Same story at the upper end. */
    ScanLog upper = {0};
    ASSERT_INT_EQ(3, btree_scan_range(tree, &below, 1, &beta, 1,
                                      scan_log_fn, &upper));
    ASSERT_INT_EQ(1, log_has(&upper, 1, 1));
    ASSERT_INT_EQ(1, log_has(&upper, 2, 2));
    ASSERT_INT_EQ(1, log_has(&upper, 3, 3));

    btree_destroy(tree);
    pager_close(pager);
    cleanup(path);
}

/* The empty string is the floor of the string key space, so it fences a scan
   off the int, float and NULL keys that sort below it. */
TEST(btree_empty_string_bound_fences_off_other_key_types) {
    char* path = make_temp_path();
    Pager* pager = pager_open(path);
    BTree* tree = btree_create(pager);

    Cell i = int_key(7);
    Cell f = float_key(2.5);
    Cell null_key;
    null_key.type = VAL_NULL;
    Cell s = string_key("apple");
    ASSERT_INT_EQ(1, btree_insert(tree, &i, 1, 1));
    ASSERT_INT_EQ(1, btree_insert(tree, &f, 2, 2));
    ASSERT_INT_EQ(1, btree_insert(tree, &null_key, 3, 3));
    ASSERT_INT_EQ(1, btree_insert(tree, &s, 4, 4));

    Cell floor_bound = string_key("");
    ScanLog log = {0};
    ASSERT_INT_EQ(1, btree_scan_range(tree, &floor_bound, 1, NULL, 0,
                                      scan_log_fn, &log));
    ASSERT_INT_EQ(1, log_has(&log, 4, 4));

    btree_destroy(tree);
    pager_close(pager);
    cleanup(path);
}

TEST(btree_duplicate_keys_all_found) {
    char* path = make_temp_path();
    Pager* pager = pager_open(path);
    BTree* tree = btree_create(pager);

    Cell k = int_key(42);
    for (int i = 0; i < 5; i++) {
        ASSERT_INT_EQ(1, btree_insert(tree, &k, 10 + i, i));
    }

    ScanLog log = {0};
    ASSERT_INT_EQ(5, btree_scan_eq(tree, &k, scan_log_fn, &log));
    for (int i = 0; i < 5; i++) {
        ASSERT_INT_EQ(1, log_has(&log, 10 + i, i));
    }

    btree_destroy(tree);
    pager_close(pager);
    cleanup(path);
}

TEST(btree_delete_removes_only_target_locator) {
    char* path = make_temp_path();
    Pager* pager = pager_open(path);
    BTree* tree = btree_create(pager);

    Cell k = int_key(7);
    for (int i = 0; i < 4; i++) {
        ASSERT_INT_EQ(1, btree_insert(tree, &k, 20 + i, i));
    }
    Cell other = int_key(8);
    ASSERT_INT_EQ(1, btree_insert(tree, &other, 99, 1));

    ASSERT_INT_EQ(1, btree_delete(tree, &k, 21, 1));

    ScanLog log = {0};
    ASSERT_INT_EQ(3, btree_scan_eq(tree, &k, scan_log_fn, &log));
    ASSERT_INT_EQ(0, log_has(&log, 21, 1));
    ASSERT_INT_EQ(1, log_has(&log, 20, 0));
    ASSERT_INT_EQ(1, log_has(&log, 22, 2));
    ASSERT_INT_EQ(1, log_has(&log, 23, 3));

    /* Deleting again must fail; deleting a missing key must fail. */
    ASSERT_INT_EQ(0, btree_delete(tree, &k, 21, 1));
    Cell missing = int_key(1234);
    ASSERT_INT_EQ(0, btree_delete(tree, &missing, 1, 1));

    /* The neighbour key is untouched. */
    ScanLog log2 = {0};
    ASSERT_INT_EQ(1, btree_scan_eq(tree, &other, scan_log_fn, &log2));
    ASSERT_INT_EQ(1, log_has(&log2, 99, 1));

    btree_destroy(tree);
    pager_close(pager);
    cleanup(path);
}

TEST(btree_delete_across_many_keys_keeps_search_correct) {
    char* path = make_temp_path();
    Pager* pager = pager_open(path);
    BTree* tree = btree_create(pager);

    for (int i = 0; i < 400; i++) {
        Cell k = int_key(i);
        ASSERT_INT_EQ(1, btree_insert(tree, &k, i, i));
    }
    /* Delete every third key. */
    for (int i = 0; i < 400; i += 3) {
        Cell k = int_key(i);
        ASSERT_INT_EQ(1, btree_delete(tree, &k, i, i));
    }
    for (int i = 0; i < 400; i++) {
        Cell k = int_key(i);
        ScanLog log = {0};
        int n = btree_scan_eq(tree, &k, scan_log_fn, &log);
        if (i % 3 == 0) {
            ASSERT_INT_EQ(0, n);
        } else {
            ASSERT_INT_EQ(1, n);
            ASSERT_INT_EQ(1, log_has(&log, i, i));
        }
    }

    btree_destroy(tree);
    pager_close(pager);
    cleanup(path);
}

TEST(btree_numeric_ordering_int_and_float_spaces) {
    char* path = make_temp_path();
    Pager* pager = pager_open(path);
    BTree* tree = btree_create(pager);

    /* Int space: negative ints order correctly. */
    Cell neg = int_key(-50);
    Cell zero = int_key(0);
    Cell pos = int_key(50);
    btree_insert(tree, &pos, 1, 1);
    btree_insert(tree, &neg, 2, 2);
    btree_insert(tree, &zero, 3, 3);

    ScanLog log = {0};
    int n = btree_scan_range(tree, &neg, 1, &zero, 1, scan_log_fn, &log);
    ASSERT_INT_EQ(2, n);
    ASSERT_INT_EQ(1, log_has(&log, 2, 2)); /* -50 */
    ASSERT_INT_EQ(1, log_has(&log, 3, 3)); /* 0 */

    /* Float space: negative floats order correctly. */
    BTree* ftree = btree_create(pager);
    Cell f1 = float_key(-1.5);
    Cell f2 = float_key(3.25);
    Cell f3 = float_key(0.0);
    btree_insert(ftree, &f2, 10, 1);
    btree_insert(ftree, &f1, 11, 2);
    btree_insert(ftree, &f3, 12, 3);

    ScanLog flog = {0};
    n = btree_scan_range(ftree, NULL, 0, &f3, 1, scan_log_fn, &flog);
    ASSERT_INT_EQ(2, n);
    ASSERT_INT_EQ(1, log_has(&flog, 11, 2)); /* -1.5 */
    ASSERT_INT_EQ(1, log_has(&flog, 12, 3)); /* 0.0 */

    btree_destroy(ftree);
    btree_destroy(tree);
    pager_close(pager);
    cleanup(path);
}

TEST(btree_free_pages_releases_pages) {
    char* path = make_temp_path();
    Pager* pager = pager_open(path);
    BTree* tree = btree_create(pager);

    for (int i = 0; i < 300; i++) {
        Cell k = int_key(i);
        ASSERT_INT_EQ(1, btree_insert(tree, &k, i, i));
    }
    int used_before = pager_page_count(pager);
    ASSERT(used_before > 3);

    btree_free_pages(tree);
    btree_destroy(tree);

    /* After freeing, allocation reuses low page numbers. */
    int reused = pager_allocate_page(pager);
    ASSERT(reused > 0 && reused < used_before);

    pager_close(pager);
    cleanup(path);
}

int main(void) {
    RUN_TEST(btree_insert_and_search_small);
    RUN_TEST(btree_persists_after_reopen);
    RUN_TEST(btree_sequential_inserts_split_and_search);
    RUN_TEST(btree_reverse_and_random_inserts);
    RUN_TEST(btree_string_keys_ordered_and_searchable);
    RUN_TEST(btree_string_keys_sharing_the_prefix_over_approximate);
    RUN_TEST(btree_string_range_bounds_from_truncated_literals_need_widening);
    RUN_TEST(btree_empty_string_bound_fences_off_other_key_types);
    RUN_TEST(btree_duplicate_keys_all_found);
    RUN_TEST(btree_delete_removes_only_target_locator);
    RUN_TEST(btree_delete_across_many_keys_keeps_search_correct);
    RUN_TEST(btree_numeric_ordering_int_and_float_spaces);
    RUN_TEST(btree_free_pages_releases_pages);
    TEST_SUMMARY();
}
