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

/* Shared setup for the rich-WHERE tests: rows are
   (1, "apple", 10), (2, "banana", 20), (3, "cherry", 30),
   (4, "avocado", 40), (5, "plum", null). */
#define PHASE11_W_T_SETUP \
    "    create table w_t (id int, name string, qty int);\n" \
    "    insert into w_t values (1, \"apple\", 10);\n" \
    "    insert into w_t values (2, \"banana\", 20);\n" \
    "    insert into w_t values (3, \"cherry\", 30);\n" \
    "    insert into w_t values (4, \"avocado\", 40);\n" \
    "    insert into w_t values (5, \"plum\", null);\n"

TEST(phase11_where_and_filters) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        PHASE11_W_T_SETUP
        "    int n = -1;\n"
        "    select count(*) into n from w_t where qty >= 20 and qty < 40;\n"
        "    print n;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    /* Only rows 2 (qty 20) and 3 (qty 30) match. */
    ASSERT_INT_EQ(1, output_contains(out, "2"));
}

TEST(phase11_where_or_filters) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        PHASE11_W_T_SETUP
        "    int n = -1;\n"
        "    select count(*) into n from w_t where id = 1 or id = 3 or id = 5;\n"
        "    print n;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "3"));
}

TEST(phase11_where_and_or_precedence) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        PHASE11_W_T_SETUP
        "    int n1 = -1;\n"
        "    select count(*) into n1 from w_t where id = 1 or id = 2 and qty = 20;\n"
        "    print n1;\n"
        "    int n2 = -1;\n"
        "    select count(*) into n2 from w_t where (id = 1 or id = 2) and qty = 20;\n"
        "    print n2;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    /* AND binds tighter: id=1 OR (id=2 AND qty=20) matches rows 1 and 2. */
    ASSERT_INT_EQ(1, output_contains(out, "2"));
    /* Parenthesized: (id=1 OR id=2) AND qty=20 matches only row 2. */
    ASSERT_INT_EQ(1, output_contains(out, "1"));
}

TEST(phase11_where_not_filters) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        PHASE11_W_T_SETUP
        "    int n = -1;\n"
        "    select count(*) into n from w_t where not (id = 1 or id = 2);\n"
        "    print n;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    /* Rows 3, 4, 5 remain. */
    ASSERT_INT_EQ(1, output_contains(out, "3"));
}

TEST(phase11_where_not_binds_tighter_than_and) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        PHASE11_W_T_SETUP
        "    int n = -1;\n"
        "    select count(*) into n from w_t where not id = 1 and id = 2;\n"
        "    print n;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    /* (NOT (id=1)) AND (id=2) matches only row 2. */
    ASSERT_INT_EQ(1, output_contains(out, "1"));
}

TEST(phase11_where_in_list) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        PHASE11_W_T_SETUP
        "    int n = -1;\n"
        "    select count(*) into n from w_t where id in (1, 3, 5);\n"
        "    print n;\n"
        "    int m = -1;\n"
        "    select count(*) into m from w_t where name in (\"apple\", \"plum\");\n"
        "    print m;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "3"));
    ASSERT_INT_EQ(1, output_contains(out, "2"));
}

TEST(phase11_where_not_in_list) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        PHASE11_W_T_SETUP
        "    int n = -1;\n"
        "    select count(*) into n from w_t where id not in (1, 3, 5);\n"
        "    print n;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    /* Rows 2 and 4 remain. */
    ASSERT_INT_EQ(1, output_contains(out, "2"));
}

TEST(phase11_where_in_with_null_is_three_valued) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        PHASE11_W_T_SETUP
        "    int n = -1;\n"
        "    select count(*) into n from w_t where id in (1, null);\n"
        "    print n;\n"
        "    int m = -1;\n"
        "    select count(*) into m from w_t where id not in (1, null);\n"
        "    print m;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    /* IN (1, null): row 1 matches; every other row is unknown -> filtered. */
    ASSERT_INT_EQ(1, output_contains(out, "1"));
    /* NOT IN (1, null): row 1 is false, every other row unknown -> none pass. */
    ASSERT_INT_EQ(1, output_contains(out, "0"));
}

TEST(phase11_where_like_percent) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        PHASE11_W_T_SETUP
        "    int n = -1;\n"
        "    select count(*) into n from w_t where name like \"a%\";\n"
        "    print n;\n"
        "    int m = -1;\n"
        "    select count(*) into m from w_t where name like \"%an%\";\n"
        "    print m;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    /* 'a%' matches apple and avocado; '%an%' matches banana. */
    ASSERT_INT_EQ(1, output_contains(out, "2"));
    ASSERT_INT_EQ(1, output_contains(out, "1"));
}

TEST(phase11_where_like_underscore) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        PHASE11_W_T_SETUP
        "    int n = -1;\n"
        "    select count(*) into n from w_t where name like \"_pple\";\n"
        "    print n;\n"
        "    int m = -1;\n"
        "    select count(*) into m from w_t where name like \"_____\";\n"
        "    print m;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    /* '_pple' matches apple; '_____' matches only the 5-letter apple. */
    ASSERT_INT_EQ(1, output_contains(out, "1"));
}

TEST(phase11_where_like_is_case_sensitive) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        PHASE11_W_T_SETUP
        "    int n = -1;\n"
        "    select count(*) into n from w_t where name like \"A%\";\n"
        "    print n;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    /* LIKE is case-sensitive in the custom engine: 'A%' matches nothing. */
    ASSERT_INT_EQ(1, output_contains(out, "0"));
}

TEST(phase11_where_not_like) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        PHASE11_W_T_SETUP
        "    int n = -1;\n"
        "    select count(*) into n from w_t where name not like \"a%\";\n"
        "    print n;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    /* banana, cherry, plum do not start with 'a'. */
    ASSERT_INT_EQ(1, output_contains(out, "3"));
}

TEST(phase11_where_null_comparison_stays_unknown) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        PHASE11_W_T_SETUP
        "    int n = -1;\n"
        "    select count(*) into n from w_t where qty <> 20;\n"
        "    print n;\n"
        "    int m = -1;\n"
        "    select count(*) into m from w_t where not (qty >= 20);\n"
        "    print m;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    /* qty <> 20: rows 1, 3, 4 (row 5 is NULL -> unknown -> filtered). */
    ASSERT_INT_EQ(1, output_contains(out, "3"));
    /* NOT (qty >= 20): only row 1; row 5 stays unknown even under NOT. */
    ASSERT_INT_EQ(1, output_contains(out, "1"));
}

TEST(phase11_update_compound_where) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        PHASE11_W_T_SETUP
        "    update w_t set qty = 99 where id = 1 or id = 2;\n"
        "    int n = -1;\n"
        "    select count(*) into n from w_t where qty = 99;\n"
        "    print n;\n"
        "    update w_t set qty = 7 where id >= 3 and qty is not null;\n"
        "    int m = -1;\n"
        "    select count(*) into m from w_t where qty = 7;\n"
        "    print m;\n"
        "    int q3 = -1;\n"
        "    select qty into q3 from w_t where id = 3;\n"
        "    print q3;\n"
        "    int k = -1;\n"
        "    select qty into k from w_t where id = 5;\n"
        "    print k;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    /* OR update touched rows 1 and 2; AND update touched rows 3 and 4
       (row 5 qty is NULL -> filtered), so both counts are 2. */
    ASSERT_INT_EQ(2, count_occurrences(out, "2"));
    /* Row 3 now has qty 7. */
    ASSERT_INT_EQ(1, output_contains(out, "7"));
    /* Row 5 keeps its NULL qty. */
    ASSERT_INT_EQ(1, output_contains(out, "null"));
}

TEST(phase11_delete_compound_where) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        PHASE11_W_T_SETUP
        "    delete from w_t where id in (2, 4) or qty is null;\n"
        "    int n = -1;\n"
        "    select count(*) into n from w_t;\n"
        "    print n;\n"
        "    delete from w_t where id = 1 and name = \"apple\";\n"
        "    int m = -1;\n"
        "    select count(*) into m from w_t;\n"
        "    print m;\n"
        "    int x = -1;\n"
        "    select id into x from w_t;\n"
        "    print x;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    /* IN/OR delete removed rows 2, 4, 5; rows 1 and 3 remain. */
    ASSERT_INT_EQ(1, output_contains(out, "2"));
    /* AND delete removed row 1; only row 3 remains. */
    ASSERT_INT_EQ(1, output_contains(out, "1"));
    ASSERT_INT_EQ(1, output_contains(out, "3"));
}

TEST(phase11_join_compound_where) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table j_a (id int, k int);\n"
        "    create table j_b (k int, tag string);\n"
        "    insert into j_a values (1, 100);\n"
        "    insert into j_a values (2, 200);\n"
        "    insert into j_a values (3, 300);\n"
        "    insert into j_b values (100, \"x\");\n"
        "    insert into j_b values (200, \"y\");\n"
        "    insert into j_b values (300, \"z\");\n"
        "    int n = -1;\n"
        "    select count(*) into n from j_a join j_b on j_a.k = j_b.k "
        "        where j_a.id = 1 or j_b.tag = \"z\";\n"
        "    print n;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    /* Rows for id 1 and id 3 (tag 'z') match. */
    ASSERT_INT_EQ(1, output_contains(out, "2"));
}

TEST(phase11_create_index_and_indexed_select) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table ix_t (id int, name string);\n"
        "    insert into ix_t values (1, \"alice\");\n"
        "    insert into ix_t values (2, \"bob\");\n"
        "    insert into ix_t values (3, \"carol\");\n"
        "    create index ix_id on ix_t (id);\n"
        "    string s = \"?\";\n"
        "    select name into s from ix_t where id = 2;\n"
        "    print s;\n"
        "    int n = -1;\n"
        "    select count(*) into n from ix_t where id = 99;\n"
        "    print n;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "bob"));
    ASSERT_INT_EQ(1, output_contains(out, "0"));
}

TEST(phase11_index_covers_preexisting_rows) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table ix2_t (id int, name string);\n"
        "    insert into ix2_t values (1, \"alice\");\n"
        "    insert into ix2_t values (2, \"bob\");\n"
        "    insert into ix2_t values (3, \"carol\");\n"
        "    create index ix2_id on ix2_t (id);\n"
        "    int n = -1;\n"
        "    select count(*) into n from ix2_t where id = 1 or id = 3;\n"
        "    print n;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    /* Rows inserted before CREATE INDEX are found through the index. */
    ASSERT_INT_EQ(1, output_contains(out, "2"));
}

TEST(phase11_index_maintained_on_insert) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table ix3_t (id int, name string);\n"
        "    insert into ix3_t values (1, \"alice\");\n"
        "    create index ix3_id on ix3_t (id);\n"
        "    insert into ix3_t values (2, \"bob\");\n"
        "    insert into ix3_t values (3, \"carol\");\n"
        "    string s = \"?\";\n"
        "    select name into s from ix3_t where id = 3;\n"
        "    print s;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    /* Rows inserted after CREATE INDEX are indexed too. */
    ASSERT_INT_EQ(1, output_contains(out, "carol"));
}

TEST(phase11_index_maintained_on_update_and_delete) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table ix4_t (id int, name string);\n"
        "    insert into ix4_t values (1, \"alice\");\n"
        "    insert into ix4_t values (2, \"bob\");\n"
        "    insert into ix4_t values (3, \"carol\");\n"
        "    create index ix4_id on ix4_t (id);\n"
        "    update ix4_t set id = 5 where id = 2;\n"
        "    delete from ix4_t where id = 1;\n"
        "    int gone = -1;\n"
        "    select count(*) into gone from ix4_t where id = 2;\n"
        "    print gone;\n"
        "    int moved = -1;\n"
        "    select count(*) into moved from ix4_t where id = 5;\n"
        "    print moved;\n"
        "    int deleted = -1;\n"
        "    select count(*) into deleted from ix4_t where id = 1;\n"
        "    print deleted;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    /* id 2 moved to 5 (count 0 then 1); id 1 deleted (count 0). The CLI
       also prints main's return value, a fourth line with 0. */
    ASSERT_INT_EQ(3, count_occurrences(out, "0"));
    ASSERT_INT_EQ(1, output_contains(out, "1"));
}

TEST(phase11_index_persists_across_restarts) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table ix5_t (id int, name string);\n"
        "    insert into ix5_t values (1, \"alice\");\n"
        "    insert into ix5_t values (2, \"bob\");\n"
        "    create index ix5_id on ix5_t (id);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);

    /* New process: index metadata and tree pages are reloaded. */
    rc = run_mypl(
        "proc main() -> int {\n"
        "    string s = \"?\";\n"
        "    select name into s from ix5_t where id = 2;\n"
        "    print s;\n"
        "    insert into ix5_t values (3, \"carol\");\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "bob"));

    /* Third process: the row inserted after reopen is indexed. */
    rc = run_mypl(
        "proc main() -> int {\n"
        "    string s = \"?\";\n"
        "    select name into s from ix5_t where id = 3;\n"
        "    print s;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "carol"));
}

TEST(phase11_drop_index_keeps_scan_correct) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table ix6_t (id int, name string);\n"
        "    insert into ix6_t values (1, \"alice\");\n"
        "    insert into ix6_t values (2, \"bob\");\n"
        "    create index ix6_id on ix6_t (id);\n"
        "    drop index ix6_id;\n"
        "    string s = \"?\";\n"
        "    select name into s from ix6_t where id = 2;\n"
        "    print s;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "bob"));
}

TEST(phase11_index_on_other_column_still_correct) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table ix7_t (id int, name string);\n"
        "    insert into ix7_t values (1, \"alice\");\n"
        "    insert into ix7_t values (2, \"bob\");\n"
        "    insert into ix7_t values (3, \"carol\");\n"
        "    create index ix7_id on ix7_t (id);\n"
        "    int n = -1;\n"
        "    select count(*) into n from ix7_t where name = \"carol\";\n"
        "    print n;\n"
        "    int m = -1;\n"
        "    select count(*) into m from ix7_t where name <> \"bob\";\n"
        "    print m;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    /* name is not indexed: full-scan semantics must be unchanged. */
    ASSERT_INT_EQ(1, output_contains(out, "1"));
    ASSERT_INT_EQ(1, output_contains(out, "2"));
}

TEST(phase11_create_index_missing_table_errors) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create index ix_no on nope (id);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
}

TEST(phase11_create_index_missing_column_errors) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table ix8_t (id int);\n"
        "    create index ix8_no on ix8_t (nope);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
}

TEST(phase11_create_duplicate_index_errors) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table ix9_t (id int);\n"
        "    create index ix9_id on ix9_t (id);\n"
        "    create index ix9_id on ix9_t (id);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
}

TEST(phase11_drop_missing_index_errors) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table ix10_t (id int);\n"
        "    drop index ix10_no;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
}

TEST(phase11_drop_table_removes_its_indexes) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table ix11_t (id int, name string);\n"
        "    insert into ix11_t values (1, \"alice\");\n"
        "    create index ix11_id on ix11_t (id);\n"
        "    drop table ix11_t;\n"
        "    create table ix11_t (id int, name string);\n"
        "    insert into ix11_t values (2, \"bob\");\n"
        "    create index ix11_id on ix11_t (id);\n"
        "    string s = \"?\";\n"
        "    select name into s from ix11_t where id = 2;\n"
        "    print s;\n"
        "    int stale = -1;\n"
        "    select count(*) into stale from ix11_t where id = 1;\n"
        "    print stale;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    /* Index name reusable after drop; no stale entries from the old table. */
    ASSERT_INT_EQ(1, output_contains(out, "bob"));
    ASSERT_INT_EQ(1, output_contains(out, "0"));
}

TEST(phase11_index_range_lookup) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table ix12_t (id int, name string);\n"
        "    insert into ix12_t values (1, \"a\");\n"
        "    insert into ix12_t values (2, \"b\");\n"
        "    insert into ix12_t values (3, \"c\");\n"
        "    insert into ix12_t values (4, \"d\");\n"
        "    insert into ix12_t values (5, \"e\");\n"
        "    create index ix12_id on ix12_t (id);\n"
        "    int n = -1;\n"
        "    select count(*) into n from ix12_t where id >= 2 and id < 5;\n"
        "    print n;\n"
        "    int m = -1;\n"
        "    select count(*) into m from ix12_t where id > 4;\n"
        "    print m;\n"
        "    int k = -1;\n"
        "    select count(*) into k from ix12_t where id <= 2;\n"
        "    print k;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    /* 2..4 -> 3 rows; >4 -> 1 row; <=2 -> 2 rows. */
    ASSERT_INT_EQ(1, output_contains(out, "3"));
    ASSERT_INT_EQ(1, output_contains(out, "1"));
    ASSERT_INT_EQ(1, output_contains(out, "2"));
}

TEST(phase11_index_string_lookup) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table ix13_t (id int, name string);\n"
        "    insert into ix13_t values (1, \"alice\");\n"
        "    insert into ix13_t values (2, \"bob\");\n"
        "    insert into ix13_t values (3, \"carol\");\n"
        "    create index ix13_name on ix13_t (name);\n"
        "    int id = -1;\n"
        "    select id into id from ix13_t where name = \"bob\";\n"
        "    print id;\n"
        "    int n = -1;\n"
        "    select count(*) into n from ix13_t where name = \"nobody\";\n"
        "    print n;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "2"));
    ASSERT_INT_EQ(1, output_contains(out, "0"));
}

TEST(phase11_index_survives_alter_rebuild) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table ix14_t (id int, name string);\n"
        "    insert into ix14_t values (1, \"alice\");\n"
        "    insert into ix14_t values (2, \"bob\");\n"
        "    create index ix14_id on ix14_t (id);\n"
        "    alter table ix14_t add column tag string;\n"
        "    string s = \"?\";\n"
        "    select name into s from ix14_t where id = 2;\n"
        "    print s;\n"
        "    alter table ix14_t drop column tag;\n"
        "    string t = \"?\";\n"
        "    select name into t from ix14_t where id = 1;\n"
        "    print t;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    /* Row-chain rebuilds (ALTER) keep the index in sync. */
    ASSERT_INT_EQ(1, output_contains(out, "bob"));
    ASSERT_INT_EQ(1, output_contains(out, "alice"));
}

/* -------------------------------------------------------------------------- */
/* Column constraints: NOT NULL / PRIMARY KEY / UNIQUE / DEFAULT              */
/* -------------------------------------------------------------------------- */

TEST(phase11_not_null_rejects_null_insert) {
    remove("mypl.db");
    char out[1024];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table nn_t (id int, name string not null);\n"
        "    insert into nn_t values (1, null);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
    ASSERT_INT_EQ(1, output_contains(out, "NOT NULL constraint"));
}

TEST(phase11_not_null_accepts_values) {
    remove("mypl.db");
    char out[1024];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table nn2_t (id int not null, name string not null);\n"
        "    insert into nn2_t values (1, \"alice\");\n"
        "    insert into nn2_t values (2, \"bob\");\n"
        "    string s = \"?\";\n"
        "    select name into s from nn2_t where id = 2;\n"
        "    print s;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "bob"));
}

TEST(phase11_unique_rejects_duplicate_insert) {
    remove("mypl.db");
    char out[1024];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table uq_t (id int, email string unique);\n"
        "    insert into uq_t values (1, \"a@x\");\n"
        "    insert into uq_t values (2, \"a@x\");\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
    ASSERT_INT_EQ(1, output_contains(out, "UNIQUE constraint"));
}

TEST(phase11_unique_allows_multiple_nulls) {
    remove("mypl.db");
    char out[1024];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table uq2_t (id int, email string unique);\n"
        "    insert into uq2_t values (1, null);\n"
        "    insert into uq2_t values (2, null);\n"
        "    insert into uq2_t values (3, \"a@x\");\n"
        "    print \"ok\";\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "ok"));
}

TEST(phase11_primary_key_rejects_duplicate) {
    remove("mypl.db");
    char out[1024];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table pk_t (id int primary key, name string);\n"
        "    insert into pk_t values (1, \"alice\");\n"
        "    insert into pk_t values (1, \"bob\");\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
    ASSERT_INT_EQ(1, output_contains(out, "PRIMARY KEY constraint"));
}

TEST(phase11_primary_key_rejects_null) {
    remove("mypl.db");
    char out[1024];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table pk2_t (id int primary key, name string);\n"
        "    insert into pk2_t values (null, \"alice\");\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
    ASSERT_INT_EQ(1, output_contains(out, "PRIMARY KEY constraint"));
}

TEST(phase11_second_primary_key_rejected) {
    remove("mypl.db");
    char out[1024];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table pk3_t (id int primary key, code int primary key);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
    ASSERT_INT_EQ(1, output_contains(out, "PRIMARY KEY"));
}

TEST(phase11_default_type_mismatch_rejected) {
    remove("mypl.db");
    char out[1024];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table dm_t (id int, qty int default \"seven\");\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
    ASSERT_INT_EQ(1, output_contains(out, "DEFAULT"));
}

TEST(phase11_default_backfills_alter_add_column) {
    remove("mypl.db");
    char out[1024];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table d_t (id int);\n"
        "    insert into d_t values (1);\n"
        "    insert into d_t values (2);\n"
        "    alter table d_t add column tag string default \"new\";\n"
        "    string s = \"?\";\n"
        "    select tag into s from d_t where id = 1;\n"
        "    print s;\n"
        "    insert into d_t values (3, \"custom\");\n"
        "    select tag into s from d_t where id = 3;\n"
        "    print s;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "new"));
    ASSERT_INT_EQ(1, output_contains(out, "custom"));
}

TEST(phase11_default_fills_null_insert) {
    remove("mypl.db");
    char out[1024];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table d2_t (id int, qty int default 7);\n"
        "    insert into d2_t values (1, null);\n"
        "    insert into d2_t values (2, 3);\n"
        "    int q = -1;\n"
        "    select qty into q from d2_t where id = 1;\n"
        "    print q;\n"
        "    select qty into q from d2_t where id = 2;\n"
        "    print q;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "7"));
    ASSERT_INT_EQ(1, output_contains(out, "3"));
}

TEST(phase11_constraints_persist_across_restarts) {
    remove("mypl.db");
    char out[1024];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table p_t (id int primary key, name string not null);\n"
        "    insert into p_t values (1, \"alice\");\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);

    /* New process: the PRIMARY KEY constraint is still enforced. */
    rc = run_mypl(
        "proc main() -> int {\n"
        "    insert into p_t values (1, \"bob\");\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
    ASSERT_INT_EQ(1, output_contains(out, "PRIMARY KEY constraint"));

    /* New process: the NOT NULL constraint is still enforced. */
    rc = run_mypl(
        "proc main() -> int {\n"
        "    insert into p_t values (2, null);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
    ASSERT_INT_EQ(1, output_contains(out, "NOT NULL constraint"));

    /* New process: a valid row still inserts and reads back. */
    rc = run_mypl(
        "proc main() -> int {\n"
        "    insert into p_t values (2, \"bob\");\n"
        "    string s = \"?\";\n"
        "    select name into s from p_t where id = 2;\n"
        "    print s;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "bob"));
}

TEST(phase11_not_null_rejects_null_update) {
    remove("mypl.db");
    char out[1024];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table un_t (id int, name string not null);\n"
        "    insert into un_t values (1, \"alice\");\n"
        "    update un_t set name = null where id = 1;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
    ASSERT_INT_EQ(1, output_contains(out, "NOT NULL constraint"));

    /* New process: the rejected UPDATE left the row unchanged. */
    rc = run_mypl(
        "proc main() -> int {\n"
        "    string s = \"?\";\n"
        "    select name into s from un_t where id = 1;\n"
        "    print s;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "alice"));
}

TEST(phase11_unique_rejects_duplicate_update) {
    remove("mypl.db");
    char out[1024];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table uq3_t (id int, email string unique);\n"
        "    insert into uq3_t values (1, \"a@x\");\n"
        "    insert into uq3_t values (2, \"b@x\");\n"
        "    update uq3_t set email = \"a@x\" where id = 2;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
    ASSERT_INT_EQ(1, output_contains(out, "UNIQUE constraint"));

    /* New process: the rejected UPDATE left the row unchanged. */
    rc = run_mypl(
        "proc main() -> int {\n"
        "    string s = \"?\";\n"
        "    select email into s from uq3_t where id = 2;\n"
        "    print s;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "b@x"));
}

/* -------------------------------------------------------------------------- */
/* CREATE VIEW / DROP VIEW (custom engine)                                    */
/* -------------------------------------------------------------------------- */

TEST(phase11_create_view_and_select) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table v1_t (id int, name string, qty int);\n"
        "    insert into v1_t values (1, \"apple\", 10);\n"
        "    insert into v1_t values (2, \"banana\", 20);\n"
        "    insert into v1_t values (3, \"cherry\", 30);\n"
        "    create view v1_big as select id, name from v1_t where qty >= 20;\n"
        "    int n = -1;\n"
        "    select count(*) into n from v1_big;\n"
        "    print n;\n"
        "    string s = \"?\";\n"
        "    select name into s from v1_big where id = 2;\n"
        "    print s;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    /* The view's own WHERE filters to 2 rows. */
    ASSERT_INT_EQ(1, count_occurrences(out, "2"));
    ASSERT_INT_EQ(1, output_contains(out, "banana"));
}

TEST(phase11_view_outer_where_order_limit) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table v2_t (id int, qty int);\n"
        "    insert into v2_t values (1, 10);\n"
        "    insert into v2_t values (2, 20);\n"
        "    insert into v2_t values (3, 30);\n"
        "    create view v2_v as select id, qty from v2_t;\n"
        "    int top = -1;\n"
        "    select id into top from v2_v where qty > 5 order by qty desc limit 1;\n"
        "    print top;\n"
        "    int mid = -1;\n"
        "    select id into mid from v2_v where qty = 20;\n"
        "    print mid;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    /* Outer WHERE/ORDER BY/LIMIT compose over the view: top is id 3. */
    ASSERT_INT_EQ(1, output_contains(out, "3"));
    ASSERT_INT_EQ(1, output_contains(out, "2"));
}

TEST(phase11_view_star_select_for_row) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table v3_t (id int, name string);\n"
        "    insert into v3_t values (1, \"alice\");\n"
        "    insert into v3_t values (2, \"bob\");\n"
        "    create view v3_v as select * from v3_t;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);

    /* New process: the view persists and row fields resolve through it. */
    rc = run_mypl(
        "proc main() -> int {\n"
        "    for row in select id, name from v3_v order by id {\n"
        "        print row.id;\n"
        "        print row.name;\n"
        "    }\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "alice"));
    ASSERT_INT_EQ(1, output_contains(out, "bob"));
}

TEST(phase11_view_persists_across_restarts) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table v4_t (id int, name string, qty int);\n"
        "    insert into v4_t values (1, \"apple\", 10);\n"
        "    insert into v4_t values (2, \"banana\", 20);\n"
        "    create view v4_v as select id, name from v4_t where qty > 5;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);

    /* New process: the view definition is reloaded from the catalog. */
    rc = run_mypl(
        "proc main() -> int {\n"
        "    int n = -1;\n"
        "    select count(*) into n from v4_v;\n"
        "    print n;\n"
        "    string s = \"?\";\n"
        "    select name into s from v4_v where id = 2;\n"
        "    print s;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, count_occurrences(out, "2"));
    ASSERT_INT_EQ(1, output_contains(out, "banana"));
}

TEST(phase11_drop_view_then_select_errors) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table v5_t (id int);\n"
        "    insert into v5_t values (7);\n"
        "    create view v5_v as select id from v5_t;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);

    /* Dropping the view succeeds; the base table is untouched. */
    rc = run_mypl(
        "proc main() -> int {\n"
        "    drop view v5_v;\n"
        "    int n = -1;\n"
        "    select count(*) into n from v5_t;\n"
        "    print n;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "1"));

    /* New process: selecting from the dropped view errors. */
    rc = run_mypl(
        "proc main() -> int {\n"
        "    for row in select id from v5_v {\n"
        "        print row.id;\n"
        "    }\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
}

TEST(phase11_drop_view_if_exists) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    drop view if exists v6_v;\n"
        "    create table v6_t (id int);\n"
        "    create view v6_v as select id from v6_t;\n"
        "    drop view if exists v6_v;\n"
        "    drop view if exists v6_v;\n"
        "    print \"ok\";\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "ok"));
}

TEST(phase11_drop_missing_view_errors) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    drop view nope_v;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
}

TEST(phase11_create_view_missing_table_errors) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create view v8_v as select id from nope_t;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
}

TEST(phase11_insert_into_view_errors) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table v9_t (id int);\n"
        "    create view v9_v as select id from v9_t;\n"
        "    insert into v9_v values (1);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
    ASSERT_INT_EQ(1, output_contains(out, "view"));
}

TEST(phase11_update_view_errors) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table v10_t (id int);\n"
        "    insert into v10_t values (1);\n"
        "    create view v10_v as select id from v10_t;\n"
        "    update v10_v set id = 2;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
    ASSERT_INT_EQ(1, output_contains(out, "view"));
}

TEST(phase11_delete_view_errors) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table v11_t (id int);\n"
        "    insert into v11_t values (1);\n"
        "    create view v11_v as select id from v11_t;\n"
        "    delete from v11_v;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
    ASSERT_INT_EQ(1, output_contains(out, "view"));
}

TEST(phase11_drop_table_on_view_errors) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table v12_t (id int);\n"
        "    insert into v12_t values (5);\n"
        "    create view v12_v as select id from v12_t;\n"
        "    drop table v12_v;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
    ASSERT_INT_EQ(1, output_contains(out, "view"));

    /* The failed DROP TABLE must not have corrupted the view. */
    rc = run_mypl(
        "proc main() -> int {\n"
        "    int n = -1;\n"
        "    select count(*) into n from v12_v;\n"
        "    print n;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "1"));
}

TEST(phase11_drop_view_on_table_errors) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table v13_t (id int);\n"
        "    insert into v13_t values (9);\n"
        "    drop view v13_t;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
    ASSERT_INT_EQ(1, output_contains(out, "table"));

    /* The failed DROP VIEW must not have harmed the table. */
    rc = run_mypl(
        "proc main() -> int {\n"
        "    int n = -1;\n"
        "    select count(*) into n from v13_t;\n"
        "    print n;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "1"));
}

TEST(phase11_create_view_duplicate_name_errors) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table v14_t (id int);\n"
        "    create view v14_v as select id from v14_t;\n"
        "    create view v14_v as select id from v14_t;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
}

TEST(phase11_create_view_on_table_name_errors) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table v15_t (id int);\n"
        "    create view v15_t as select id from v15_t;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
}

TEST(phase11_view_over_view) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table v16_t (id int, qty int);\n"
        "    insert into v16_t values (1, 10);\n"
        "    insert into v16_t values (2, 20);\n"
        "    insert into v16_t values (3, 30);\n"
        "    create view v16_a as select id, qty from v16_t where qty >= 20;\n"
        "    create view v16_b as select id from v16_a where qty >= 30;\n"
        "    int n = -1;\n"
        "    select count(*) into n from v16_b;\n"
        "    print n;\n"
        "    int x = -1;\n"
        "    select id into x from v16_b;\n"
        "    print x;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "1"));
    ASSERT_INT_EQ(1, output_contains(out, "3"));
}

/* A SQL loop variable reads the current row whatever it is named; only `row`
 * used to work (examples/inventory.mypl, migration.mypl and todo.mypl). */
TEST(phase11_named_sql_loop_variable_reads_fields) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table nl_t (id int, name string);\n"
        "    insert into nl_t values (1, \"alpha\");\n"
        "    insert into nl_t values (2, \"beta\");\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);

    /* New process, so the table is in the catalog at compile time. */
    rc = run_mypl(
        "proc main() -> int {\n"
        "    int total = 0;\n"
        "    for item in select id, name from nl_t order by id {\n"
        "        total = total + item.id;\n"
        "        print concat(\"name=\", item.name);\n"
        "    }\n"
        "    print concat(\"total=\", int_to_string(total));\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "name=alpha\nname=beta\ntotal=3"));

    /* Columns are still checked against the query. */
    rc = run_mypl(
        "proc main() -> int {\n"
        "    for item in select name from nl_t {\n"
        "        print item.id;\n"
        "    }\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
    ASSERT_INT_EQ(1, output_contains(out, "Unknown column 'id' for row variable 'item'"));
    remove("mypl.db");
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
    RUN_TEST(phase11_where_and_filters);
    RUN_TEST(phase11_where_or_filters);
    RUN_TEST(phase11_where_and_or_precedence);
    RUN_TEST(phase11_where_not_filters);
    RUN_TEST(phase11_where_not_binds_tighter_than_and);
    RUN_TEST(phase11_where_in_list);
    RUN_TEST(phase11_where_not_in_list);
    RUN_TEST(phase11_where_in_with_null_is_three_valued);
    RUN_TEST(phase11_where_like_percent);
    RUN_TEST(phase11_where_like_underscore);
    RUN_TEST(phase11_where_like_is_case_sensitive);
    RUN_TEST(phase11_where_not_like);
    RUN_TEST(phase11_where_null_comparison_stays_unknown);
    RUN_TEST(phase11_update_compound_where);
    RUN_TEST(phase11_delete_compound_where);
    RUN_TEST(phase11_join_compound_where);
    RUN_TEST(phase11_create_index_and_indexed_select);
    RUN_TEST(phase11_index_covers_preexisting_rows);
    RUN_TEST(phase11_index_maintained_on_insert);
    RUN_TEST(phase11_index_maintained_on_update_and_delete);
    RUN_TEST(phase11_index_persists_across_restarts);
    RUN_TEST(phase11_drop_index_keeps_scan_correct);
    RUN_TEST(phase11_index_on_other_column_still_correct);
    RUN_TEST(phase11_create_index_missing_table_errors);
    RUN_TEST(phase11_create_index_missing_column_errors);
    RUN_TEST(phase11_create_duplicate_index_errors);
    RUN_TEST(phase11_drop_missing_index_errors);
    RUN_TEST(phase11_drop_table_removes_its_indexes);
    RUN_TEST(phase11_index_range_lookup);
    RUN_TEST(phase11_index_string_lookup);
    RUN_TEST(phase11_index_survives_alter_rebuild);
    RUN_TEST(phase11_not_null_rejects_null_insert);
    RUN_TEST(phase11_not_null_accepts_values);
    RUN_TEST(phase11_unique_rejects_duplicate_insert);
    RUN_TEST(phase11_unique_allows_multiple_nulls);
    RUN_TEST(phase11_primary_key_rejects_duplicate);
    RUN_TEST(phase11_primary_key_rejects_null);
    RUN_TEST(phase11_second_primary_key_rejected);
    RUN_TEST(phase11_default_type_mismatch_rejected);
    RUN_TEST(phase11_default_backfills_alter_add_column);
    RUN_TEST(phase11_default_fills_null_insert);
    RUN_TEST(phase11_constraints_persist_across_restarts);
    RUN_TEST(phase11_not_null_rejects_null_update);
    RUN_TEST(phase11_unique_rejects_duplicate_update);
    RUN_TEST(phase11_create_view_and_select);
    RUN_TEST(phase11_view_outer_where_order_limit);
    RUN_TEST(phase11_view_star_select_for_row);
    RUN_TEST(phase11_view_persists_across_restarts);
    RUN_TEST(phase11_drop_view_then_select_errors);
    RUN_TEST(phase11_drop_view_if_exists);
    RUN_TEST(phase11_drop_missing_view_errors);
    RUN_TEST(phase11_create_view_missing_table_errors);
    RUN_TEST(phase11_insert_into_view_errors);
    RUN_TEST(phase11_update_view_errors);
    RUN_TEST(phase11_delete_view_errors);
    RUN_TEST(phase11_drop_table_on_view_errors);
    RUN_TEST(phase11_drop_view_on_table_errors);
    RUN_TEST(phase11_create_view_duplicate_name_errors);
    RUN_TEST(phase11_create_view_on_table_name_errors);
    RUN_TEST(phase11_view_over_view);
    RUN_TEST(phase11_named_sql_loop_variable_reads_fields);
    TEST_SUMMARY();
}
