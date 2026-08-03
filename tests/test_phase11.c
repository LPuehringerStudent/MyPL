#include "test_harness.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int run_mypl(const char* source, char* out, size_t out_size) {
    FILE* f = fopen("/tmp/test_phase11_src.mypl", "w");
    if (f == NULL) return -1;
    fprintf(f, "%s", source);
    fclose(f);

    int rc = system("./bin/mypl /tmp/test_phase11_src.mypl > /tmp/test_phase11_out.txt 2>&1");

    FILE* outf = fopen("/tmp/test_phase11_out.txt", "r");
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

static int count_occurrences(const char* out, const char* substr) {
    int count = 0;
    size_t len = strlen(substr);
    const char* p = out;
    while ((p = strstr(p, substr)) != NULL) {
        count++;
        p += len;
    }
    return count;
}

TEST(phase11_null_literal_assign_and_print) {
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    int x = null;\n"
        "    print x;\n"
        "    string s = null;\n"
        "    print s;\n"
        "    print null;\n"
        "    x = 42;\n"
        "    print x;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(3, count_occurrences(out, "null"));
    ASSERT_INT_EQ(1, output_contains(out, "42"));
}

TEST(phase11_null_arithmetic_yields_null) {
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    int x = null;\n"
        "    print x + 1;\n"
        "    print 2 * x;\n"
        "    print x - x;\n"
        "    print x / 4;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(4, count_occurrences(out, "null"));
}

TEST(phase11_null_comparison_is_three_valued) {
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    int x = null;\n"
        "    if x == null {\n"
        "        print \"eq-true\";\n"
        "    } else {\n"
        "        print \"eq-unknown\";\n"
        "    }\n"
        "    if x < 5 {\n"
        "        print \"lt-true\";\n"
        "    } else {\n"
        "        print \"lt-unknown\";\n"
        "    }\n"
        "    print x == 5;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "eq-unknown"));
    ASSERT_INT_EQ(1, output_contains(out, "lt-unknown"));
    ASSERT_INT_EQ(0, output_contains(out, "eq-true"));
    ASSERT_INT_EQ(0, output_contains(out, "lt-true"));
    /* The comparison result itself is null (three-valued logic). */
    ASSERT_INT_EQ(1, output_contains(out, "null"));
}

TEST(phase11_null_condition_is_not_true) {
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    if null {\n"
        "        print \"taken\";\n"
        "    } else {\n"
        "        print \"not-taken\";\n"
        "    }\n"
        "    bool b = null;\n"
        "    if b {\n"
        "        print \"b-taken\";\n"
        "    } else {\n"
        "        print \"b-not-taken\";\n"
        "    }\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "not-taken"));
    ASSERT_INT_EQ(1, output_contains(out, "b-not-taken"));
    ASSERT_INT_EQ(0, output_contains(out, "b-taken"));
}

TEST(phase11_insert_null_select_returns_null) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table n_t (id int, name string);\n"
        "    insert into n_t values (1, \"alice\");\n"
        "    insert into n_t values (2, null);\n"
        "    string s = \"placeholder\";\n"
        "    select name into s from n_t where id = 2;\n"
        "    print s;\n"
        "    int i = 0;\n"
        "    select id into i from n_t where id = 1;\n"
        "    print i;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "null"));
    ASSERT_INT_EQ(1, output_contains(out, "1"));
}

TEST(phase11_where_is_null_filters) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table n_t (id int, name string);\n"
        "    insert into n_t values (1, \"alice\");\n"
        "    insert into n_t values (2, null);\n"
        "    int y = -1;\n"
        "    select id into y from n_t where name is null;\n"
        "    print y;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "2"));
}

TEST(phase11_where_is_not_null_filters) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table n_t (id int, name string);\n"
        "    insert into n_t values (1, \"alice\");\n"
        "    insert into n_t values (2, null);\n"
        "    int y = -1;\n"
        "    select id into y from n_t where name is not null;\n"
        "    print y;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "1"));
}

TEST(phase11_aggregates_skip_null) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table n_t (id int, val int);\n"
        "    insert into n_t values (1, 10);\n"
        "    insert into n_t values (2, null);\n"
        "    insert into n_t values (3, 30);\n"
        "    int total = -1;\n"
        "    select count(*) into total from n_t;\n"
        "    print total;\n"
        "    int c = -1;\n"
        "    select count(val) into c from n_t;\n"
        "    print c;\n"
        "    int s = -1;\n"
        "    select sum(val) into s from n_t;\n"
        "    print s;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    /* COUNT(*) counts all rows. */
    ASSERT_INT_EQ(1, output_contains(out, "3"));
    /* COUNT(col) skips NULLs. */
    ASSERT_INT_EQ(1, output_contains(out, "2"));
    /* SUM skips NULLs: 10 + 30 = 40. */
    ASSERT_INT_EQ(1, output_contains(out, "40"));
}

TEST(phase11_coalesce_returns_first_non_null) {
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    int x = null;\n"
        "    print coalesce(x, 5);\n"
        "    print coalesce(null, null);\n"
        "    print coalesce(7, 9);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "5"));
    ASSERT_INT_EQ(1, output_contains(out, "null"));
    ASSERT_INT_EQ(1, output_contains(out, "7"));
}

TEST(phase11_nvl_alias) {
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    string s = null;\n"
        "    print nvl(s, \"fallback\");\n"
        "    print nvl(\"keep\", \"fallback\");\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "fallback"));
    ASSERT_INT_EQ(1, output_contains(out, "keep"));
}

TEST(phase11_drop_table_recreate_works) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table d_t (id int);\n"
        "    insert into d_t values (1);\n"
        "    drop table d_t;\n"
        "    create table d_t (id int, tag string);\n"
        "    insert into d_t values (2, \"new\");\n"
        "    int n = -1;\n"
        "    select count(*) into n from d_t;\n"
        "    print n;\n"
        "    string s = \"?\";\n"
        "    select tag into s from d_t where id = 2;\n"
        "    print s;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    /* Only the row inserted after the recreate is present. */
    ASSERT_INT_EQ(1, output_contains(out, "1"));
    ASSERT_INT_EQ(1, output_contains(out, "new"));

    /* The recreated table persists across process restarts. */
    rc = run_mypl(
        "proc main() -> int {\n"
        "    int n = -1;\n"
        "    select count(*) into n from d_t;\n"
        "    print n;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "1"));
}

TEST(phase11_drop_missing_table_errors) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    drop table nope;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
}

TEST(phase11_drop_table_if_exists) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    drop table if exists maybe_t;\n"
        "    create table maybe_t (id int);\n"
        "    drop table if exists maybe_t;\n"
        "    drop table if exists maybe_t;\n"
        "    print \"ok\";\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "ok"));
}

TEST(phase11_alter_add_column) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table a_t (id int, name string);\n"
        "    insert into a_t values (1, \"alice\");\n"
        "    alter table a_t add column age int;\n"
        "    insert into a_t values (2, \"bob\", 42);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);

    /* New process: the added column persists; old rows read back as null. */
    rc = run_mypl(
        "proc main() -> int {\n"
        "    int old_age = -1;\n"
        "    select age into old_age from a_t where id = 1;\n"
        "    print old_age;\n"
        "    int new_age = -1;\n"
        "    select age into new_age from a_t where id = 2;\n"
        "    print new_age;\n"
        "    string nm = \"?\";\n"
        "    select name into nm from a_t where id = 1;\n"
        "    print nm;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "null"));
    ASSERT_INT_EQ(1, output_contains(out, "42"));
    ASSERT_INT_EQ(1, output_contains(out, "alice"));
}

TEST(phase11_alter_add_column_insert_needs_new_column) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table a2_t (id int);\n"
        "    alter table a2_t add column tag string;\n"
        "    insert into a2_t values (1);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    /* Too few values after ADD COLUMN is an error. */
    ASSERT_INT_EQ(1, rc);
}

TEST(phase11_alter_drop_column) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table dc_t (id int, name string);\n"
        "    insert into dc_t values (1, \"alice\");\n"
        "    insert into dc_t values (2, \"bob\");\n"
        "    alter table dc_t drop column name;\n"
        "    insert into dc_t values (3);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);

    /* New process: remaining column keeps its data across the rebuild. */
    rc = run_mypl(
        "proc main() -> int {\n"
        "    int n = -1;\n"
        "    select count(*) into n from dc_t;\n"
        "    print n;\n"
        "    int x = -1;\n"
        "    select id into x from dc_t where id = 2;\n"
        "    print x;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "3"));
    ASSERT_INT_EQ(1, output_contains(out, "2"));

    /* Selecting the dropped column is an error. */
    rc = run_mypl(
        "proc main() -> int {\n"
        "    for row in select name from dc_t {\n"
        "        print row.name;\n"
        "    }\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
}

TEST(phase11_alter_missing_table_errors) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    alter table nope add column x int;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
}

int main(void) {
    RUN_TEST(phase11_null_literal_assign_and_print);
    RUN_TEST(phase11_null_arithmetic_yields_null);
    RUN_TEST(phase11_null_comparison_is_three_valued);
    RUN_TEST(phase11_null_condition_is_not_true);
    RUN_TEST(phase11_insert_null_select_returns_null);
    RUN_TEST(phase11_where_is_null_filters);
    RUN_TEST(phase11_where_is_not_null_filters);
    RUN_TEST(phase11_aggregates_skip_null);
    RUN_TEST(phase11_coalesce_returns_first_non_null);
    RUN_TEST(phase11_nvl_alias);
    RUN_TEST(phase11_drop_table_recreate_works);
    RUN_TEST(phase11_drop_missing_table_errors);
    RUN_TEST(phase11_drop_table_if_exists);
    RUN_TEST(phase11_alter_add_column);
    RUN_TEST(phase11_alter_add_column_insert_needs_new_column);
    RUN_TEST(phase11_alter_drop_column);
    RUN_TEST(phase11_alter_missing_table_errors);
    TEST_SUMMARY();
}
