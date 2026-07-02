#include "test_harness.h"
#include <sqlite3.h>

TEST(sqlite3_is_linked) {
    sqlite3* db = NULL;
    int rc = sqlite3_open(":memory:", &db);
    ASSERT_INT_EQ(SQLITE_OK, rc);
    sqlite3_close(db);
}

int main(void) {
    RUN_TEST(sqlite3_is_linked);
    TEST_SUMMARY();
}
