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

TEST(cli_initializes_package_declared_in_imported_module) {
    system("rm -rf /tmp/mypl_import_pkginit_test");
    system("mkdir -p /tmp/mypl_import_pkginit_test/lib");

    FILE* f = fopen("/tmp/mypl_import_pkginit_test/lib/counter.mypl", "w");
    ASSERT_PTR_NOT_NULL(f);
    fprintf(f,
        "package counter is\n"
        "    counter int;\n"
        "    func get() -> int;\n"
        "end counter;\n"
        "\n"
        "package body counter is\n"
        "    int counter = 41;\n"
        "    func get() -> int {\n"
        "        return counter;\n"
        "    }\n"
        "end counter;\n");
    fclose(f);

    f = fopen("/tmp/mypl_import_pkginit_test/main.mypl", "w");
    ASSERT_PTR_NOT_NULL(f);
    fprintf(f, "import \"lib/counter.mypl\";\nproc main() -> int { return counter.get(); }\n");
    fclose(f);

    /* Package-level state declared in the imported module must run its
       initializer before main() executes, exactly as if it were declared
       in the main file itself. Without that, `counter` starts at the
       int zero-value instead of 41. */
    int rc = system("./bin/mypl /tmp/mypl_import_pkginit_test/main.mypl > /tmp/mypl_out.txt 2>&1");
    ASSERT_INT_EQ(0, WEXITSTATUS(rc));

    FILE* out = fopen("/tmp/mypl_out.txt", "r");
    ASSERT_PTR_NOT_NULL(out);
    char buf[64];
    fgets(buf, sizeof(buf), out);
    fclose(out);
    ASSERT_INT_EQ(41, atoi(buf));

    system("rm -rf /tmp/mypl_import_pkginit_test");
}

TEST(cli_initializes_packages_from_multiple_imported_modules_in_order) {
    system("rm -rf /tmp/mypl_import_pkginit_multi_test");
    system("mkdir -p /tmp/mypl_import_pkginit_multi_test/lib");

    FILE* f = fopen("/tmp/mypl_import_pkginit_multi_test/lib/a.mypl", "w");
    ASSERT_PTR_NOT_NULL(f);
    fprintf(f,
        "package pkg_a is\n"
        "    func get() -> int;\n"
        "end pkg_a;\n"
        "\n"
        "package body pkg_a is\n"
        "    int value = 10;\n"
        "    func get() -> int {\n"
        "        return value;\n"
        "    }\n"
        "end pkg_a;\n");
    fclose(f);

    f = fopen("/tmp/mypl_import_pkginit_multi_test/lib/b.mypl", "w");
    ASSERT_PTR_NOT_NULL(f);
    fprintf(f,
        "package pkg_b is\n"
        "    func get() -> int;\n"
        "end pkg_b;\n"
        "\n"
        "package body pkg_b is\n"
        "    int value = 20;\n"
        "    func get() -> int {\n"
        "        return value;\n"
        "    }\n"
        "end pkg_b;\n");
    fclose(f);

    f = fopen("/tmp/mypl_import_pkginit_multi_test/main.mypl", "w");
    ASSERT_PTR_NOT_NULL(f);
    fprintf(f,
        "import \"lib/a.mypl\";\n"
        "import \"lib/b.mypl\";\n"
        "proc main() -> int { return pkg_a.get() + pkg_b.get(); }\n");
    fclose(f);

    int rc = system("./bin/mypl /tmp/mypl_import_pkginit_multi_test/main.mypl > /tmp/mypl_out.txt 2>&1");
    ASSERT_INT_EQ(0, WEXITSTATUS(rc));

    FILE* out = fopen("/tmp/mypl_out.txt", "r");
    ASSERT_PTR_NOT_NULL(out);
    char buf[64];
    fgets(buf, sizeof(buf), out);
    fclose(out);
    ASSERT_INT_EQ(30, atoi(buf));

    system("rm -rf /tmp/mypl_import_pkginit_multi_test");
}

TEST(cli_accepts_conditional_flags_in_any_order) {
    FILE* f = fopen("/tmp/cli_cc.mypl", "w");
    ASSERT_PTR_NOT_NULL(f);
    fprintf(f,
            "$if DEBUG $then\n"
            "func debug_value() -> int { return 40; }\n"
            "$else\n"
            "func debug_value() -> int { return 0; }\n"
            "$end\n"
            "proc main() -> int {\n"
            "$if TRACE $then\n"
            "    return debug_value() + 2;\n"
            "$else\n"
            "    return 0;\n"
            "$end\n"
            "}\n");
    fclose(f);

    int rc = system("./bin/mypl -DDEBUG /tmp/cli_cc.mypl -DTRACE -DDEBUG > /tmp/mypl_out.txt 2>&1");
    ASSERT_INT_EQ(0, WEXITSTATUS(rc));
    FILE* out = fopen("/tmp/mypl_out.txt", "r");
    ASSERT_PTR_NOT_NULL(out);
    char buf[64];
    ASSERT_PTR_NOT_NULL(fgets(buf, sizeof(buf), out));
    fclose(out);
    ASSERT_INT_EQ(42, atoi(buf));
    remove("/tmp/cli_cc.mypl");
}

TEST(cli_conditional_flags_apply_to_imports) {
    system("rm -rf /tmp/mypl_cc_import_test");
    system("mkdir -p /tmp/mypl_cc_import_test");

    FILE* f = fopen("/tmp/mypl_cc_import_test/helper.mypl", "w");
    ASSERT_PTR_NOT_NULL(f);
    fprintf(f,
            "$if FEATURE $then\n"
            "proc feature_value() -> int { return 42; }\n"
            "$else\n"
            "proc feature_value() -> int { return 0; }\n"
            "$end\n");
    fclose(f);

    f = fopen("/tmp/mypl_cc_import_test/main.mypl", "w");
    ASSERT_PTR_NOT_NULL(f);
    fprintf(f,
            "import \"helper.mypl\";\n"
            "proc main() -> int { return feature_value(); }\n");
    fclose(f);

    int rc = system("./bin/mypl -DFEATURE /tmp/mypl_cc_import_test/main.mypl > /tmp/mypl_out.txt 2>&1");
    ASSERT_INT_EQ(0, WEXITSTATUS(rc));
    FILE* out = fopen("/tmp/mypl_out.txt", "r");
    ASSERT_PTR_NOT_NULL(out);
    char buf[64];
    ASSERT_PTR_NOT_NULL(fgets(buf, sizeof(buf), out));
    fclose(out);
    ASSERT_INT_EQ(42, atoi(buf));
    system("rm -rf /tmp/mypl_cc_import_test");
}

TEST(cli_rejects_invalid_conditional_flag) {
    int rc = system("./bin/mypl -DDEBUG=1 tests/fixtures/add.mypl > /tmp/mypl_out.txt 2>&1");
    ASSERT_INT_EQ(1, WEXITSTATUS(rc));
    FILE* out = fopen("/tmp/mypl_out.txt", "r");
    ASSERT_PTR_NOT_NULL(out);
    char buf[256] = {0};
    ASSERT_PTR_NOT_NULL(fgets(buf, sizeof(buf), out));
    fclose(out);
    ASSERT_PTR_NOT_NULL(strstr(buf, "Invalid conditional-compilation flag"));
}

TEST(cli_rejects_conditional_flag_without_file) {
    int rc = system("./bin/mypl -DDEBUG > /tmp/mypl_out.txt 2>&1");
    ASSERT_INT_EQ(1, WEXITSTATUS(rc));
}

#ifdef USE_SQLITE
TEST(cli_accepts_db_flag) {
    remove("/tmp/cli_test.db");
    FILE* f = fopen("/tmp/cli_db.mypl", "w");
    ASSERT_PTR_NOT_NULL(f);
    fprintf(f,
            "proc main() -> int {\n"
            "$if DB_TEST $then\n"
            "    create table t (id int);\n"
            "$end\n"
            "    return 0;\n"
            "}\n");
    fclose(f);
    int rc = system("./bin/mypl /tmp/cli_db.mypl --db /tmp/cli_test.db -DDB_TEST > /tmp/cli_db_out.txt 2>&1");
    ASSERT_INT_EQ(0, WEXITSTATUS(rc));
    remove("/tmp/cli_db.mypl");
    remove("/tmp/cli_test.db");
}
#endif

int main(void) {
    RUN_TEST(cli_runs_file_and_prints_int_result);
    RUN_TEST(cli_runs_file_and_prints_float_result);
    RUN_TEST(cli_returns_nonzero_on_compile_error);
    RUN_TEST(cli_resolves_import_relative_to_importing_file);
    RUN_TEST(cli_resolves_nested_import_relative_to_importing_file);
    RUN_TEST(cli_initializes_package_declared_in_imported_module);
    RUN_TEST(cli_initializes_packages_from_multiple_imported_modules_in_order);
    RUN_TEST(cli_accepts_conditional_flags_in_any_order);
    RUN_TEST(cli_conditional_flags_apply_to_imports);
    RUN_TEST(cli_rejects_invalid_conditional_flag);
    RUN_TEST(cli_rejects_conditional_flag_without_file);
#ifdef USE_SQLITE
    RUN_TEST(cli_accepts_db_flag);
#endif
    TEST_SUMMARY();
}
