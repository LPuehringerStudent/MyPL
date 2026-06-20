#include "test_harness.h"
#include "sql_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PAGE_SIZE 4096

static char* make_temp_path(void) {
    char* path = malloc(256);
    if (path == NULL) return NULL;
    snprintf(path, 256, "/tmp/mydb_test_pager_%d.db", (int)getpid());
    unlink(path);
    return path;
}

static void cleanup(const char* path) {
    unlink(path);
    free((void*)path);
}

TEST(pager_creates_file_and_allocates_page) {
    char* path = make_temp_path();
    Pager* pager = pager_open(path);
    ASSERT_PTR_NOT_NULL(pager);

    int page = pager_allocate_page(pager);
    ASSERT_INT_EQ(2, page);

    uint8_t data[PAGE_SIZE];
    memset(data, 0xAB, PAGE_SIZE);
    pager_write_page(pager, page, data);

    uint8_t read[PAGE_SIZE];
    pager_read_page(pager, page, read);
    ASSERT_INT_EQ(0, memcmp(data, read, PAGE_SIZE));

    pager_close(pager);
    cleanup(path);
}

TEST(pager_preserves_data_after_reopen) {
    char* path = make_temp_path();
    Pager* pager = pager_open(path);
    ASSERT_PTR_NOT_NULL(pager);

    int page = pager_allocate_page(pager);
    uint8_t data[PAGE_SIZE];
    memset(data, 0xCD, PAGE_SIZE);
    pager_write_page(pager, page, data);
    pager_close(pager);

    pager = pager_open(path);
    ASSERT_PTR_NOT_NULL(pager);

    uint8_t read[PAGE_SIZE];
    pager_read_page(pager, page, read);
    ASSERT_INT_EQ(0, memcmp(data, read, PAGE_SIZE));

    pager_close(pager);
    cleanup(path);
}

TEST(pager_allocates_multiple_pages_and_tracks_free_list) {
    char* path = make_temp_path();
    Pager* pager = pager_open(path);
    ASSERT_PTR_NOT_NULL(pager);

    int a = pager_allocate_page(pager);
    int b = pager_allocate_page(pager);
    int c = pager_allocate_page(pager);
    ASSERT_INT_EQ(2, a);
    ASSERT_INT_EQ(3, b);
    ASSERT_INT_EQ(4, c);

    pager_free_page(pager, b);

    int d = pager_allocate_page(pager);
    ASSERT_INT_EQ(3, d);

    int e = pager_allocate_page(pager);
    ASSERT_INT_EQ(5, e);

    pager_close(pager);
    cleanup(path);
}

TEST(pager_reports_header_and_catalog_pages_for_new_file) {
    char* path = make_temp_path();
    Pager* pager = pager_open(path);
    ASSERT_PTR_NOT_NULL(pager);
    ASSERT_INT_EQ(2, pager_page_count(pager));
    pager_close(pager);
    cleanup(path);
}

int main(void) {
    RUN_TEST(pager_creates_file_and_allocates_page);
    RUN_TEST(pager_preserves_data_after_reopen);
    RUN_TEST(pager_allocates_multiple_pages_and_tracks_free_list);
    RUN_TEST(pager_reports_header_and_catalog_pages_for_new_file);
    TEST_SUMMARY();
}
