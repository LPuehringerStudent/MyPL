#include "test_harness.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int run_repl(const char* input, char* output, size_t output_size) {
    FILE* in = fopen("/tmp/repl_in.txt", "w");
    if (in == NULL) {
        output[0] = '\0';
        return -1;
    }
    fprintf(in, "%s", input);
    fclose(in);

    int rc = system("timeout 5 ./bin/mypl < /tmp/repl_in.txt > /tmp/repl_out.txt 2>&1");
    FILE* f = fopen("/tmp/repl_out.txt", "r");
    if (f == NULL) {
        output[0] = '\0';
        return rc;
    }
    size_t n = fread(output, 1, output_size - 1, f);
    output[n] = '\0';
    fclose(f);
    return rc;
}

static int output_contains(const char* output, const char* needle) {
    return strstr(output, needle) != NULL;
}

TEST(repl_defines_and_calls_procedure) {
    char out[4096];
    run_repl("proc double(n int) -> int { return n * 2; }\ndouble(21)\n.exit\n",
             out, sizeof(out));
    ASSERT_INT_EQ(1, output_contains(out, "42"));
}

TEST(repl_persists_variables) {
    char out[4096];
    run_repl("int x = 5;\nx + 1\n.exit\n", out, sizeof(out));
    ASSERT_INT_EQ(1, output_contains(out, "6"));
}

TEST(repl_inspects_variables) {
    char out[4096];
    run_repl("int x = 5;\n.vars\n.exit\n", out, sizeof(out));
    ASSERT_INT_EQ(1, output_contains(out, "x = 5"));
}

TEST(repl_loads_file) {
    FILE* f = fopen("/tmp/repl_load_test.mypl", "w");
    ASSERT_PTR_NOT_NULL(f);
    fprintf(f, "proc triple(n int) -> int { return n * 3; }\n");
    fclose(f);

    char out[4096];
    run_repl(".load /tmp/repl_load_test.mypl\ntriple(7)\n.exit\n",
             out, sizeof(out));
    ASSERT_INT_EQ(1, output_contains(out, "21"));
}

TEST(repl_lists_tables_and_schema) {
    char out[4096];
    run_repl(".sql CREATE TABLE users (id INT, name STRING)\n"
             ".tables\n"
             ".schema\n"
             ".exit\n",
             out, sizeof(out));
    ASSERT_INT_EQ(1, output_contains(out, "users"));
    ASSERT_INT_EQ(1, output_contains(out, "id int"));
    ASSERT_INT_EQ(1, output_contains(out, "name string"));
}

TEST(repl_executes_sql_select) {
    char out[4096];
    run_repl(".sql CREATE TABLE items (id INT, label STRING)\n"
             ".sql INSERT INTO items VALUES (1, 'a')\n"
             ".sql INSERT INTO items VALUES (2, 'b')\n"
             ".sql SELECT id, label FROM items\n"
             ".exit\n",
             out, sizeof(out));
    ASSERT_INT_EQ(1, output_contains(out, "id = 1"));
    ASSERT_INT_EQ(1, output_contains(out, "label = a"));
    ASSERT_INT_EQ(1, output_contains(out, "id = 2"));
    ASSERT_INT_EQ(1, output_contains(out, "label = b"));
}

TEST(repl_shows_defined_procedures) {
    char out[4096];
    run_repl("proc inc(n int) -> int { return n + 1; }\n.defs\n.exit\n",
             out, sizeof(out));
    ASSERT_INT_EQ(1, output_contains(out, "proc inc"));
}

TEST(repl_reports_single_error_for_invalid_input) {
    char out[4096];
    run_repl("var x = 5;\n.exit\n", out, sizeof(out));
    ASSERT_INT_EQ(1, output_contains(out, "Compile error"));
    int parse_error_count = 0;
    const char* p = out;
    while ((p = strstr(p, "Parse error")) != NULL) {
        parse_error_count++;
        p++;
    }
    ASSERT_INT_EQ(1, parse_error_count);
}

TEST(repl_connects_to_database) {
    system("rm -f /tmp/repl_test.db");
    char out[4096];
    run_repl(".connect /tmp/repl_test.db\ncreate table t (id int);\n.tables\n.exit\n", out, sizeof(out));
    ASSERT_INT_EQ(1, output_contains(out, "t"));
    remove("/tmp/repl_test.db");
}

int main(void) {
    system("rm -f mypl.db");
    RUN_TEST(repl_defines_and_calls_procedure);
    RUN_TEST(repl_persists_variables);
    RUN_TEST(repl_inspects_variables);
    RUN_TEST(repl_loads_file);
    RUN_TEST(repl_lists_tables_and_schema);
    RUN_TEST(repl_executes_sql_select);
    RUN_TEST(repl_shows_defined_procedures);
    RUN_TEST(repl_reports_single_error_for_invalid_input);
    RUN_TEST(repl_connects_to_database);
    TEST_SUMMARY();
}
