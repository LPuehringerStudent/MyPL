#include "test_harness.h"
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

TEST(cli_runs_file_and_prints_int_result) {
    int rc = system("./bin/mydb tests/fixtures/add.mypl > /tmp/mypl_out.txt 2>&1");
    ASSERT_INT_EQ(0, WEXITSTATUS(rc));
    FILE* f = fopen("/tmp/mypl_out.txt", "r");
    char buf[64];
    fgets(buf, sizeof(buf), f);
    fclose(f);
    ASSERT_INT_EQ(3, atoi(buf));
}

TEST(cli_runs_file_and_prints_float_result) {
    int rc = system("./bin/mydb tests/fixtures/float.mypl > /tmp/mypl_out.txt 2>&1");
    ASSERT_INT_EQ(0, WEXITSTATUS(rc));
    FILE* f = fopen("/tmp/mypl_out.txt", "r");
    char buf[64];
    fgets(buf, sizeof(buf), f);
    fclose(f);
    ASSERT_FLOAT_EQ(3.14, atof(buf));
}

TEST(cli_returns_nonzero_on_compile_error) {
    int rc = system("./bin/mydb tests/fixtures/error.mypl > /dev/null 2>&1");
    ASSERT_INT_EQ(1, WEXITSTATUS(rc));
}

int main(void) {
    RUN_TEST(cli_runs_file_and_prints_int_result);
    RUN_TEST(cli_runs_file_and_prints_float_result);
    RUN_TEST(cli_returns_nonzero_on_compile_error);
    TEST_SUMMARY();
}
