#include "test_harness.h"
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

TEST(cli_runs_file_and_prints_int_result) {
    int rc = system("./bin/mypl tests/fixtures/add.mypl > /tmp/mypl_out.txt 2>&1");
    ASSERT_INT_EQ(0, WEXITSTATUS(rc));
    FILE* f = fopen("/tmp/mypl_out.txt", "r");
    char buf[64];
    fgets(buf, sizeof(buf), f);
    fclose(f);
    ASSERT_INT_EQ(3, atoi(buf));
}

TEST(cli_runs_file_and_prints_float_result) {
    int rc = system("./bin/mypl tests/fixtures/float.mypl > /tmp/mypl_out.txt 2>&1");
    ASSERT_INT_EQ(0, WEXITSTATUS(rc));
    FILE* f = fopen("/tmp/mypl_out.txt", "r");
    char buf[64];
    fgets(buf, sizeof(buf), f);
    fclose(f);
    ASSERT_FLOAT_EQ(3.14, atof(buf));
}

TEST(cli_returns_nonzero_on_compile_error) {
    int rc = system("./bin/mypl tests/fixtures/error.mypl > /dev/null 2>&1");
    ASSERT_INT_EQ(1, WEXITSTATUS(rc));
}

TEST(cli_resolves_import_relative_to_importing_file) {
    system("rm -rf /tmp/mypl_import_test");
    system("mkdir -p /tmp/mypl_import_test/lib");

    FILE* f = fopen("/tmp/mypl_import_test/lib/helper.mypl", "w");
    ASSERT_PTR_NOT_NULL(f);
    fprintf(f, "proc double(n int) -> int { return n * 2; }\n");
    fclose(f);

    f = fopen("/tmp/mypl_import_test/main.mypl", "w");
    ASSERT_PTR_NOT_NULL(f);
    fprintf(f, "import \"lib/helper.mypl\";\nproc main() -> int { return double(21); }\n");
    fclose(f);

    int rc = system("./bin/mypl /tmp/mypl_import_test/main.mypl > /tmp/mypl_out.txt 2>&1");
    ASSERT_INT_EQ(0, WEXITSTATUS(rc));

    FILE* out = fopen("/tmp/mypl_out.txt", "r");
    ASSERT_PTR_NOT_NULL(out);
    char buf[64];
    fgets(buf, sizeof(buf), out);
    fclose(out);
    ASSERT_INT_EQ(42, atoi(buf));

    system("rm -rf /tmp/mypl_import_test");
}

TEST(cli_resolves_nested_import_relative_to_importing_file) {
    system("rm -rf /tmp/mypl_nested_import_test");
    system("mkdir -p /tmp/mypl_nested_import_test/lib");

    FILE* f = fopen("/tmp/mypl_nested_import_test/lib/utils.mypl", "w");
    ASSERT_PTR_NOT_NULL(f);
    fprintf(f, "proc triple(n int) -> int { return n * 3; }\n");
    fclose(f);

    f = fopen("/tmp/mypl_nested_import_test/lib/helper.mypl", "w");
    ASSERT_PTR_NOT_NULL(f);
    fprintf(f, "import \"utils.mypl\";\nproc call_triple(n int) -> int { return triple(n); }\n");
    fclose(f);

    f = fopen("/tmp/mypl_nested_import_test/main.mypl", "w");
    ASSERT_PTR_NOT_NULL(f);
    fprintf(f, "import \"lib/helper.mypl\";\nproc main() -> int { return call_triple(7); }\n");
    fclose(f);

    int rc = system("./bin/mypl /tmp/mypl_nested_import_test/main.mypl > /tmp/mypl_out.txt 2>&1");
    ASSERT_INT_EQ(0, WEXITSTATUS(rc));

    FILE* out = fopen("/tmp/mypl_out.txt", "r");
    ASSERT_PTR_NOT_NULL(out);
    char buf[64];
    fgets(buf, sizeof(buf), out);
    fclose(out);
    ASSERT_INT_EQ(21, atoi(buf));

    system("rm -rf /tmp/mypl_nested_import_test");
}

TEST(cli_accepts_db_flag) {
    FILE* f = fopen("/tmp/cli_db.mypl", "w");
    ASSERT_PTR_NOT_NULL(f);
    fprintf(f, "proc main() -> int { create table t (id int); return 0; }\n");
    fclose(f);
    int rc = system("./bin/mypl /tmp/cli_db.mypl --db /tmp/cli_test.db > /tmp/cli_db_out.txt 2>&1");
    ASSERT_INT_EQ(0, WEXITSTATUS(rc));
    remove("/tmp/cli_db.mypl");
    remove("/tmp/cli_test.db");
}

int main(void) {
    RUN_TEST(cli_runs_file_and_prints_int_result);
    RUN_TEST(cli_runs_file_and_prints_float_result);
    RUN_TEST(cli_returns_nonzero_on_compile_error);
    RUN_TEST(cli_resolves_import_relative_to_importing_file);
    RUN_TEST(cli_resolves_nested_import_relative_to_importing_file);
    RUN_TEST(cli_accepts_db_flag);
    TEST_SUMMARY();
}
