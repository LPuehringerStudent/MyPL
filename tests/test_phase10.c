#include "test_harness.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int run_mypl(const char* source, char* out, size_t out_size) {
    FILE* f = fopen("/tmp/test_phase10_src.mypl", "w");
    if (f == NULL) return -1;
    fprintf(f, "%s", source);
    fclose(f);

    int rc = system("./bin/mypl /tmp/test_phase10_src.mypl > /tmp/test_phase10_out.txt 2>&1");

    FILE* outf = fopen("/tmp/test_phase10_out.txt", "r");
    if (outf != NULL) {
        out[0] = '\0';
        size_t n = fread(out, 1, out_size - 1, outf);
        out[n] = '\0';
        fclose(outf);
    }
    return WEXITSTATUS(rc);
}

static int output_contains(const char* out, const char* substr) {
    return strstr(out, substr) != NULL;
}

TEST(phase10_trigger_before_after_insert) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "trigger t_log after insert on t {\n"
        "    dbms_output.put_line(\"after\");\n"
        "}\n"
        "trigger t_pre before insert on t {\n"
        "    dbms_output.put_line(\"before\");\n"
        "}\n"
        "proc main() -> int {\n"
        "    dbms_output.enable(10);\n"
        "    create table t (id int);\n"
        "    insert into t values (1);\n"
        "    array<string> lines = dbms_output.get_lines();\n"
        "    for line in lines {\n"
        "        print line;\n"
        "    }\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "before"));
    ASSERT_INT_EQ(1, output_contains(out, "after"));
    /* before must fire before after */
    const char* b = strstr(out, "before");
    const char* a = strstr(out, "after");
    ASSERT(b != NULL && a != NULL && b < a);
}

TEST(phase10_trigger_does_not_fire_on_other_table) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "trigger t_wrong after insert on other_table {\n"
        "    dbms_output.put_line(\"wrong\");\n"
        "}\n"
        "proc main() -> int {\n"
        "    dbms_output.enable(10);\n"
        "    create table t (id int);\n"
        "    insert into t values (1);\n"
        "    array<string> lines = dbms_output.get_lines();\n"
        "    print int_to_string(length(lines));\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(0, output_contains(out, "wrong"));
    ASSERT_INT_EQ(1, output_contains(out, "0"));
}

TEST(phase10_trigger_ddl_create_table) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "trigger t_ddl after create on t2 {\n"
        "    dbms_output.put_line(\"created\");\n"
        "}\n"
        "proc main() -> int {\n"
        "    dbms_output.enable(10);\n"
        "    create table t2 (id int);\n"
        "    array<string> lines = dbms_output.get_lines();\n"
        "    for line in lines {\n"
        "        print line;\n"
        "    }\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "created"));
}

TEST(phase10_trigger_update_and_delete) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "trigger t_upd after update on t {\n"
        "    dbms_output.put_line(\"updated\");\n"
        "}\n"
        "trigger t_del after delete on t {\n"
        "    dbms_output.put_line(\"deleted\");\n"
        "}\n"
        "proc main() -> int {\n"
        "    dbms_output.enable(10);\n"
        "    create table t (id int);\n"
        "    insert into t values (1);\n"
        "    update t set id = 2 where id = 1;\n"
        "    delete from t where id = 2;\n"
        "    array<string> lines = dbms_output.get_lines();\n"
        "    for line in lines {\n"
        "        print line;\n"
        "    }\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "updated"));
    ASSERT_INT_EQ(1, output_contains(out, "deleted"));
}

TEST(phase10_table_function_scalar_collection) {
    char out[256];
    int rc = run_mypl(
        "func numbers() -> array<int> {\n"
        "    return range(1, 5);\n"
        "}\n"
        "proc main() -> int {\n"
        "    int total = 0;\n"
        "    for n in numbers() {\n"
        "        total = total + n;\n"
        "    }\n"
        "    print int_to_string(total);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "10"));
}

TEST(phase10_table_function_row_collection) {
    remove("mypl.db");
    char out[256];
    int rc = run_mypl(
        "func all_accounts() -> array<row> {\n"
        "    array<row> rows;\n"
        "    select * into rows from accounts;\n"
        "    return rows;\n"
        "}\n"
        "proc main() -> int {\n"
        "    create table accounts (id int, name string);\n"
        "    insert into accounts values (1, 'alice');\n"
        "    insert into accounts values (2, 'bob');\n"
        "    print int_to_string(length(all_accounts()));\n"
        "    for r in all_accounts() {\n"
        "        print r.name;\n"
        "    }\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "2"));
    ASSERT_INT_EQ(1, output_contains(out, "alice"));
    ASSERT_INT_EQ(1, output_contains(out, "bob"));
}

int main(void) {
    RUN_TEST(phase10_trigger_before_after_insert);
    RUN_TEST(phase10_trigger_does_not_fire_on_other_table);
    RUN_TEST(phase10_trigger_ddl_create_table);
    RUN_TEST(phase10_trigger_update_and_delete);
    RUN_TEST(phase10_table_function_scalar_collection);
    RUN_TEST(phase10_table_function_row_collection);
    TEST_SUMMARY();
}
