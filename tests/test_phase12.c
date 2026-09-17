#include "test_harness.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int run_mypl(const char* source, char* out, size_t out_size) {
    FILE* f = fopen("/tmp/test_phase12_src.mypl", "w");
    if (f == NULL) return -1;
    fprintf(f, "%s", source);
    fclose(f);

    int rc = system("./bin/mypl /tmp/test_phase12_src.mypl > /tmp/test_phase12_out.txt 2>&1");

    FILE* outf = fopen("/tmp/test_phase12_out.txt", "r");
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

static void clean_trigger_db(void) {
    remove("mypl.db");
    remove("mypl.db.programs");
}

TEST(phase12_nextval_persists_across_restarts) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create_sequence(\"seq_a\", 100, 5);\n"
        "    print nextval(\"seq_a\");\n"
        "    print nextval(\"seq_a\");\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "100"));
    ASSERT_INT_EQ(1, output_contains(out, "105"));

    /* New process: the sequence continues where it left off. */
    rc = run_mypl(
        "proc main() -> int {\n"
        "    print nextval(\"seq_a\");\n"
        "    print currval(\"seq_a\");\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(2, count_occurrences(out, "110"));
}

TEST(phase12_currval_after_restart) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create_sequence(\"seq_b\", 7, 3);\n"
        "    print nextval(\"seq_b\");\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "7"));

    /* New process: currval works without calling nextval first. */
    rc = run_mypl(
        "proc main() -> int {\n"
        "    print currval(\"seq_b\");\n"
        "    print nextval(\"seq_b\");\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "7"));
    ASSERT_INT_EQ(1, output_contains(out, "10"));
}

TEST(phase12_drop_sequence_persists) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create_sequence(\"seq_c\", 1, 1);\n"
        "    print nextval(\"seq_c\");\n"
        "    drop_sequence(\"seq_c\");\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);

    /* New process: the dropped sequence stays gone. */
    rc = run_mypl(
        "proc main() -> int {\n"
        "    print nextval(\"seq_c\");\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
    ASSERT_INT_EQ(1, output_contains(out, "sequence does not exist"));
}

TEST(phase12_currval_before_nextval_errors) {
    remove("mypl.db");
    char out[512];
    /* currval before the first nextval is an error (has_value semantics). */
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create_sequence(\"seq_d\", 50, 1);\n"
        "    print currval(\"seq_d\");\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);

    /* The sequence exists after that run, but nextval was never called, so
       currval must still error in a new process. */
    rc = run_mypl(
        "proc main() -> int {\n"
        "    print currval(\"seq_d\");\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);

    /* The first nextval in a new process still returns the start value. */
    rc = run_mypl(
        "proc main() -> int {\n"
        "    print nextval(\"seq_d\");\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "50"));
}

TEST(phase12_sequence_survives_other_catalog_writes) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create_sequence(\"seq_e\", 1000, 10);\n"
        "    print nextval(\"seq_e\");\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "1000"));

    /* Other DDL/DML rewrites the catalog page; the sequence must survive. */
    rc = run_mypl(
        "proc main() -> int {\n"
        "    create table t12 (id int, tag string);\n"
        "    insert into t12 values (1, \"x\");\n"
        "    print nextval(\"seq_e\");\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "1010"));

    rc = run_mypl(
        "proc main() -> int {\n"
        "    print nextval(\"seq_e\");\n"
        "    int n = -1;\n"
        "    select count(*) into n from t12;\n"
        "    print n;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "1020"));
    ASSERT_INT_EQ(1, output_contains(out, "1"));
}

TEST(phase12_duplicate_create_after_restart_errors) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create_sequence(\"seq_f\", 1, 1);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);

    /* New process: re-creating a persisted sequence is a duplicate. */
    rc = run_mypl(
        "proc main() -> int {\n"
        "    create_sequence(\"seq_f\", 1, 1);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
}

TEST(phase12_missing_sequence_errors_unchanged) {
    remove("mypl.db");
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    print nextval(\"nope_seq\");\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
}

/* --- Row-level triggers (FOR EACH ROW, :new / :old) --- */

TEST(phase12_row_trigger_insert_fires_per_row) {
    clean_trigger_db();
    char out[512];
    int rc = run_mypl(
        "trigger trg_r_ins after insert on trg_ri for each row {\n"
        "    print \"row-ins\";\n"
        "    print :new.id;\n"
        "    print :new.name;\n"
        "}\n"
        "proc main() -> int {\n"
        "    create table trg_ri (id int, name string);\n"
        "    insert into trg_ri values (1, \"alice\");\n"
        "    insert into trg_ri values (2, \"bob\");\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(2, count_occurrences(out, "row-ins"));
    ASSERT_INT_EQ(1, count_occurrences(out, "alice"));
    ASSERT_INT_EQ(1, count_occurrences(out, "bob"));
    clean_trigger_db();
}

TEST(phase12_row_trigger_update_sees_old_and_new) {
    clean_trigger_db();
    char out[512];
    int rc = run_mypl(
        "trigger trg_r_upd after update on trg_ru for each row {\n"
        "    print \"upd\";\n"
        "    print :old.qty;\n"
        "    print :new.qty;\n"
        "}\n"
        "proc main() -> int {\n"
        "    create table trg_ru (id int, qty int);\n"
        "    insert into trg_ru values (1, 10);\n"
        "    insert into trg_ru values (2, 20);\n"
        "    insert into trg_ru values (3, 30);\n"
        "    update trg_ru set qty = 99 where id > 1;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    /* two rows matched: (20 -> 99) and (30 -> 99) */
    ASSERT_INT_EQ(2, count_occurrences(out, "upd"));
    ASSERT_INT_EQ(1, count_occurrences(out, "20"));
    ASSERT_INT_EQ(1, count_occurrences(out, "30"));
    ASSERT_INT_EQ(2, count_occurrences(out, "99"));
    clean_trigger_db();
}

TEST(phase12_row_trigger_delete_sees_old) {
    clean_trigger_db();
    char out[512];
    int rc = run_mypl(
        "trigger trg_r_del after delete on trg_rd for each row {\n"
        "    print \"del\";\n"
        "    print :old.id;\n"
        "}\n"
        "proc main() -> int {\n"
        "    create table trg_rd (id int);\n"
        "    insert into trg_rd values (1);\n"
        "    insert into trg_rd values (2);\n"
        "    delete from trg_rd where id = 2;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, count_occurrences(out, "del"));
    ASSERT_INT_EQ(1, count_occurrences(out, "2"));
    clean_trigger_db();
}

TEST(phase12_row_and_statement_triggers_both_fire) {
    clean_trigger_db();
    char out[512];
    int rc = run_mypl(
        "trigger trg_o_sb before insert on trg_ro {\n"
        "    print \"stmt-before\";\n"
        "}\n"
        "trigger trg_o_rb before insert on trg_ro for each row {\n"
        "    print \"row-before\";\n"
        "}\n"
        "trigger trg_o_ra after insert on trg_ro for each row {\n"
        "    print \"row-after\";\n"
        "}\n"
        "trigger trg_o_sa after insert on trg_ro {\n"
        "    print \"stmt-after\";\n"
        "}\n"
        "proc main() -> int {\n"
        "    create table trg_ro (id int);\n"
        "    insert into trg_ro values (1);\n"
        "    insert into trg_ro values (2);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    /* statement-level fires once per statement, row-level once per row */
    ASSERT_INT_EQ(2, count_occurrences(out, "stmt-before"));
    ASSERT_INT_EQ(2, count_occurrences(out, "stmt-after"));
    ASSERT_INT_EQ(2, count_occurrences(out, "row-before"));
    ASSERT_INT_EQ(2, count_occurrences(out, "row-after"));
    /* ordering within one statement:
       statement BEFORE -> row BEFORE -> write -> row AFTER -> statement AFTER */
    const char* sb = strstr(out, "stmt-before");
    const char* rb = strstr(out, "row-before");
    const char* ra = strstr(out, "row-after");
    const char* sa = strstr(out, "stmt-after");
    ASSERT(sb != NULL && rb != NULL && ra != NULL && sa != NULL);
    ASSERT(sb < rb && rb < ra && ra < sa);
    clean_trigger_db();
}

TEST(phase12_row_trigger_persists_across_restarts) {
    clean_trigger_db();
    char out[512];
    int rc = run_mypl(
        "trigger trg_r_per after insert on trg_rp for each row {\n"
        "    print \"row-persist-fired\";\n"
        "    print :new.id;\n"
        "}\n"
        "proc main() -> int {\n"
        "    create table trg_rp (id int);\n"
        "    insert into trg_rp values (1);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, count_occurrences(out, "row-persist-fired"));

    /* New process, source no longer declares the trigger: the persisted
       FOR EACH ROW definition is recompiled and keeps firing with :new. */
    rc = run_mypl(
        "proc main() -> int {\n"
        "    insert into trg_rp values (2);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, count_occurrences(out, "row-persist-fired"));
    ASSERT_INT_EQ(1, count_occurrences(out, "2"));
    clean_trigger_db();
}

TEST(phase12_row_trigger_fires_on_dynamic_sql) {
    clean_trigger_db();
    char out[512];
    int rc = run_mypl(
        "trigger trg_r_dyn after insert on trg_rdy for each row {\n"
        "    print \"dyn-row\";\n"
        "    print :new.id;\n"
        "}\n"
        "proc main() -> int {\n"
        "    create table trg_rdy (id int);\n"
        "    execute_immediate(\"insert into trg_rdy values (7)\");\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, count_occurrences(out, "dyn-row"));
    ASSERT_INT_EQ(1, count_occurrences(out, "7"));
    clean_trigger_db();
}

TEST(phase12_row_trigger_wrong_context_errors) {
    clean_trigger_db();
    char out[512];
    /* :new is not available in a DELETE trigger: runtime error. */
    int rc = run_mypl(
        "trigger trg_r_bad after delete on trg_rb for each row {\n"
        "    print :new.id;\n"
        "}\n"
        "proc main() -> int {\n"
        "    create table trg_rb (id int);\n"
        "    insert into trg_rb values (1);\n"
        "    delete from trg_rb where id = 1;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
    ASSERT_INT_EQ(1, output_contains(out, "Cannot access field on non-row value"));
    clean_trigger_db();
}

TEST(phase12_row_trigger_requires_dml_event) {
    clean_trigger_db();
    char out[512];
    /* FOR EACH ROW only makes sense for row-changing events. */
    int rc = run_mypl(
        "trigger trg_r_ddl after create on trg_rddl for each row {\n"
        "    print \"nope\";\n"
        "}\n"
        "proc main() -> int {\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
    ASSERT_INT_EQ(1, output_contains(out, "'for each row' requires an insert, update, or delete trigger"));
    clean_trigger_db();
}

/* --- dbms_sql cursor API (Phase 12 Task 4) --- */

static void clean_dbms_sql_db(void) {
    remove("mypl.db");
    remove("mypl.db.programs");
}

TEST(phase12_dbms_sql_open_parse_execute_dml) {
    clean_dbms_sql_db();
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table dbms_t (id int, name string);\n"
        "    int c = dbms_sql.open_cursor();\n"
        "    dbms_sql.parse(c, \"insert into dbms_t values (1, 'alice')\");\n"
        "    int n = dbms_sql.execute_cursor(c);\n"
        "    print n;\n"
        "    dbms_sql.close_cursor(c);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "1"));
    clean_dbms_sql_db();
}

TEST(phase12_dbms_sql_fetch_rows) {
    clean_dbms_sql_db();
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table dbms_t (id int, name string);\n"
        "    insert into dbms_t values (1, 'alice');\n"
        "    insert into dbms_t values (2, 'bob');\n"
        "    int c = dbms_sql.open_cursor();\n"
        "    dbms_sql.parse(c, \"select id, name from dbms_t order by id\");\n"
        "    dbms_sql.execute_cursor(c);\n"
        "    array<row> rows = dbms_sql.fetch_rows(c, 10);\n"
        "    print length(rows);\n"
        "    print rows[0].id;\n"
        "    print rows[0].name;\n"
        "    print rows[1].id;\n"
        "    print rows[1].name;\n"
        "    dbms_sql.close_cursor(c);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "2"));
    ASSERT_INT_EQ(1, output_contains(out, "1"));
    ASSERT_INT_EQ(1, output_contains(out, "alice"));
    ASSERT_INT_EQ(1, output_contains(out, "2"));
    ASSERT_INT_EQ(1, output_contains(out, "bob"));
    clean_dbms_sql_db();
}

TEST(phase12_dbms_sql_column_value) {
    clean_dbms_sql_db();
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    create table dbms_t (id int, name string);\n"
        "    insert into dbms_t values (7, 'carol');\n"
        "    int c = dbms_sql.open_cursor();\n"
        "    dbms_sql.parse(c, \"select id, name from dbms_t\");\n"
        "    dbms_sql.execute_cursor(c);\n"
        "    array<row> rows = dbms_sql.fetch_rows(c, 1);\n"
        "    any id = dbms_sql.column_value(c, 0);\n"
        "    any name = dbms_sql.column_value(c, 1);\n"
        "    print id;\n"
        "    print name;\n"
        "    dbms_sql.close_cursor(c);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "7"));
    ASSERT_INT_EQ(1, output_contains(out, "carol"));
    clean_dbms_sql_db();
}

TEST(phase12_dbms_sql_close_invalidates_handle) {
    clean_dbms_sql_db();
    char out[512];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    int c = dbms_sql.open_cursor();\n"
        "    dbms_sql.close_cursor(c);\n"
        "    dbms_sql.execute_cursor(c);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
    ASSERT_INT_EQ(1, output_contains(out, "invalid cursor handle"));
    clean_dbms_sql_db();
}

static int build_marshal_shared_lib(void) {
    FILE* f = fopen("/tmp/test_phase12_marshal.c", "w");
    if (f == NULL) return 0;
    fprintf(f,
        "#include <string.h>\n"
        "#include <stdio.h>\n"
        "double ext_half(int x) { return x / 2.0; }\n"
        "double ext_scale(double x) { return x * 2.5; }\n"
        "int ext_floor(double x) { return (int)x; }\n"
        "int ext_len(const char* s) { return (int)strlen(s); }\n"
        "const char* ext_greet(const char* s) {\n"
        "    static char buf[64];\n"
        "    snprintf(buf, sizeof(buf), \"hello, %%s\", s);\n"
        "    return buf;\n"
        "}\n"
        "const char* ext_label(int n) { return n > 0 ? \"positive\" : \"non-positive\"; }\n"
        "const char* ext_float_label(double x) { return x < 0 ? \"neg\" : \"nonneg\"; }\n"
        "const char* ext_null(const char* s) { (void)s; return NULL; }\n");
    fclose(f);
    int rc = system("cc -shared -fPIC -o /tmp/test_phase12_marshal.so /tmp/test_phase12_marshal.c");
    return rc == 0;
}

TEST(phase12_external_call_f_return) {
    ASSERT_INT_EQ(1, build_marshal_shared_lib());
    char out[256];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    float h = external_call_f(\"/tmp/test_phase12_marshal.so\", \"ext_half\", 7);\n"
        "    float s = external_call_f(\"/tmp/test_phase12_marshal.so\", \"ext_scale\", 1.5);\n"
        "    print float_to_string(h);\n"
        "    print float_to_string(s);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "3.5"));
    ASSERT_INT_EQ(1, output_contains(out, "3.75"));
}

TEST(phase12_external_call_int_return_from_float_and_string) {
    ASSERT_INT_EQ(1, build_marshal_shared_lib());
    char out[256];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    int f = external_call(\"/tmp/test_phase12_marshal.so\", \"ext_floor\", 9.75);\n"
        "    int n = external_call(\"/tmp/test_phase12_marshal.so\", \"ext_len\", \"marshal\");\n"
        "    print int_to_string(f);\n"
        "    print int_to_string(n);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "9\n7\n"));
}

TEST(phase12_external_call_s_return) {
    ASSERT_INT_EQ(1, build_marshal_shared_lib());
    char out[256];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    string g = external_call_s(\"/tmp/test_phase12_marshal.so\", \"ext_greet\", \"mypl\");\n"
        "    string a = external_call_s(\"/tmp/test_phase12_marshal.so\", \"ext_label\", 3);\n"
        "    string b = external_call_s(\"/tmp/test_phase12_marshal.so\", \"ext_float_label\", -0.5);\n"
        "    print g;\n"
        "    print a;\n"
        "    print b;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "hello, mypl\npositive\nneg\n"));
}

TEST(phase12_external_call_s_result_is_copied) {
    ASSERT_INT_EQ(1, build_marshal_shared_lib());
    char out[256];
    /* ext_greet reuses one static buffer; the first result must not change. */
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    string first = external_call_s(\"/tmp/test_phase12_marshal.so\", \"ext_greet\", \"one\");\n"
        "    string second = external_call_s(\"/tmp/test_phase12_marshal.so\", \"ext_greet\", \"two\");\n"
        "    print first;\n"
        "    print second;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "hello, one\nhello, two\n"));
}

TEST(phase12_external_call_s_null_return_is_null) {
    ASSERT_INT_EQ(1, build_marshal_shared_lib());
    char out[256];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    print nvl(external_call_s(\"/tmp/test_phase12_marshal.so\", \"ext_null\", \"x\"), \"fallback\");\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "fallback"));
}

TEST(phase12_external_call_rejects_bool_argument) {
    char out[256];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    external_call_f(\"/tmp/test_phase12_marshal.so\", \"ext_scale\", true);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
    ASSERT_INT_EQ(1, output_contains(out, "external_call_f expects an int, float or string argument"));
}

TEST(phase12_external_call_s_result_type_is_checked) {
    char out[256];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    int n = external_call_s(\"/tmp/test_phase12_marshal.so\", \"ext_label\", 1);\n"
        "    return n;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
}

TEST(phase12_external_call_s_missing_symbol_fails) {
    ASSERT_INT_EQ(1, build_marshal_shared_lib());
    char out[256];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    external_call_s(\"/tmp/test_phase12_marshal.so\", \"no_such_symbol\", \"x\");\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
    ASSERT_INT_EQ(1, output_contains(out, "external_call_s:"));
}

#ifdef USE_SQLITE
static int run_mypl_sqlite(const char* source, char* out, size_t out_size) {
    FILE* f = fopen("/tmp/test_phase12_sql_src.mypl", "w");
    if (f == NULL) return -1;
    fprintf(f, "%s", source);
    fclose(f);

    int rc = system("./bin/mypl /tmp/test_phase12_sql_src.mypl --db /tmp/test_phase12.db"
                    " > /tmp/test_phase12_sql_out.txt 2>&1");

    FILE* outf = fopen("/tmp/test_phase12_sql_out.txt", "r");
    if (outf != NULL) {
        out[0] = '\0';
        size_t n = fread(out, 1, out_size - 1, outf);
        out[n] = '\0';
        fclose(outf);
    }
    return WEXITSTATUS(rc);
}

TEST(phase12_trigger_static_fires) {
    clean_trigger_db();
    char out[512];
    int rc = run_mypl(
        "trigger trg_static after insert on trg_s {\n"
        "    print \"static-fired\";\n"
        "}\n"
        "proc main() -> int {\n"
        "    create table trg_s (id int);\n"
        "    insert into trg_s values (1);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, count_occurrences(out, "static-fired"));
    clean_trigger_db();
}

TEST(phase12_trigger_persists_across_restarts) {
    clean_trigger_db();
    char out[512];
    int rc = run_mypl(
        "trigger trg_persist after insert on trg_p {\n"
        "    print \"persist-fired\";\n"
        "}\n"
        "proc main() -> int {\n"
        "    create table trg_p (id int);\n"
        "    insert into trg_p values (1);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, count_occurrences(out, "persist-fired"));

    /* New process, source no longer declares the trigger: it is reloaded
       from the persisted program units and still fires. */
    rc = run_mypl(
        "proc main() -> int {\n"
        "    insert into trg_p values (2);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, count_occurrences(out, "persist-fired"));
    clean_trigger_db();
}

TEST(phase12_drop_trigger_stops_and_persists) {
    clean_trigger_db();
    char out[512];
    int rc = run_mypl(
        "trigger trg_drop after insert on trg_d {\n"
        "    print \"drop-fired\";\n"
        "}\n"
        "proc main() -> int {\n"
        "    create table trg_d (id int);\n"
        "    insert into trg_d values (1);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, count_occurrences(out, "drop-fired"));

    /* DROP TRIGGER stops firing immediately ... */
    rc = run_mypl(
        "proc main() -> int {\n"
        "    drop trigger trg_drop;\n"
        "    insert into trg_d values (2);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(0, count_occurrences(out, "drop-fired"));

    /* ... and the trigger stays dropped after a restart. */
    rc = run_mypl(
        "proc main() -> int {\n"
        "    insert into trg_d values (3);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(0, count_occurrences(out, "drop-fired"));
    clean_trigger_db();
}

TEST(phase12_trigger_fires_on_execute_immediate) {
    clean_trigger_db();
    char out[512];
    int rc = run_mypl(
        "trigger trg_dyn_pre before insert on trg_dyn {\n"
        "    print \"dyn-before\";\n"
        "}\n"
        "trigger trg_dyn_post after insert on trg_dyn {\n"
        "    print \"dyn-after\";\n"
        "}\n"
        "proc main() -> int {\n"
        "    create table trg_dyn (id int);\n"
        "    execute_immediate(\"insert into trg_dyn values (1)\");\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, count_occurrences(out, "dyn-before"));
    ASSERT_INT_EQ(1, count_occurrences(out, "dyn-after"));
    /* before must fire before after */
    const char* b = strstr(out, "dyn-before");
    const char* a = strstr(out, "dyn-after");
    ASSERT(b != NULL && a != NULL && b < a);
    clean_trigger_db();
}

TEST(phase12_trigger_fires_on_dbms_sql_execute) {
    clean_trigger_db();
    char out[512];
    int rc = run_mypl(
        "trigger trg_dsql after insert on trg_ds {\n"
        "    print \"dbmssql-fired\";\n"
        "}\n"
        "proc main() -> int {\n"
        "    create table trg_ds (id int);\n"
        "    dbms_sql.execute(\"insert into trg_ds values (1)\");\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, count_occurrences(out, "dbmssql-fired"));
    clean_trigger_db();
}

TEST(phase12_drop_trigger_via_execute_immediate) {
    clean_trigger_db();
    char out[512];
    int rc = run_mypl(
        "trigger trg_dyndrop after insert on trg_dd {\n"
        "    print \"dyndrop-fired\";\n"
        "}\n"
        "proc main() -> int {\n"
        "    create table trg_dd (id int);\n"
        "    execute_immediate(\"insert into trg_dd values (1)\");\n"
        "    execute_immediate(\"drop trigger trg_dyndrop\");\n"
        "    execute_immediate(\"insert into trg_dd values (2)\");\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    /* fires for the first insert only */
    ASSERT_INT_EQ(1, count_occurrences(out, "dyndrop-fired"));

    /* The dynamic drop persists: no firing in a new process. */
    rc = run_mypl(
        "proc main() -> int {\n"
        "    insert into trg_dd values (3);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(0, count_occurrences(out, "dyndrop-fired"));
    clean_trigger_db();
}

TEST(phase12_sqlite_trigger_persists_across_restarts) {
    remove("/tmp/test_phase12_trg.db");
    char out[512];
    int rc = run_mypl_sqlite(
        "trigger strg_persist after insert on strg_p {\n"
        "    print \"sqlite-persist-fired\";\n"
        "}\n"
        "proc main() -> int {\n"
        "    create table strg_p (id int);\n"
        "    insert into strg_p values (1);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, count_occurrences(out, "sqlite-persist-fired"));

    rc = run_mypl_sqlite(
        "proc main() -> int {\n"
        "    insert into strg_p values (2);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, count_occurrences(out, "sqlite-persist-fired"));
    remove("/tmp/test_phase12_trg.db");
}

TEST(phase12_sqlite_drop_trigger_persists) {
    remove("/tmp/test_phase12_trg.db");
    char out[512];
    int rc = run_mypl_sqlite(
        "trigger strg_drop after insert on strg_d {\n"
        "    print \"sqlite-drop-fired\";\n"
        "}\n"
        "proc main() -> int {\n"
        "    create table strg_d (id int);\n"
        "    insert into strg_d values (1);\n"
        "    drop trigger strg_drop;\n"
        "    insert into strg_d values (2);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, count_occurrences(out, "sqlite-drop-fired"));

    rc = run_mypl_sqlite(
        "proc main() -> int {\n"
        "    insert into strg_d values (3);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(0, count_occurrences(out, "sqlite-drop-fired"));
    remove("/tmp/test_phase12_trg.db");
}

TEST(phase12_sqlite_nextval_persists_across_restarts) {
    remove("/tmp/test_phase12.db");
    char out[512];
    int rc = run_mypl_sqlite(
        "proc main() -> int {\n"
        "    create_sequence(\"sseq\", 200, 25);\n"
        "    print nextval(\"sseq\");\n"
        "    print nextval(\"sseq\");\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "200"));
    ASSERT_INT_EQ(1, output_contains(out, "225"));

    /* New process against the same SQLite file: currval and nextval
       continue from the persisted state. */
    rc = run_mypl_sqlite(
        "proc main() -> int {\n"
        "    print currval(\"sseq\");\n"
        "    print nextval(\"sseq\");\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "225"));
    ASSERT_INT_EQ(1, output_contains(out, "250"));
}

TEST(phase12_sqlite_drop_sequence_persists) {
    remove("/tmp/test_phase12.db");
    char out[512];
    int rc = run_mypl_sqlite(
        "proc main() -> int {\n"
        "    create_sequence(\"sseq2\", 1, 1);\n"
        "    print nextval(\"sseq2\");\n"
        "    drop_sequence(\"sseq2\");\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);

    rc = run_mypl_sqlite(
        "proc main() -> int {\n"
        "    print nextval(\"sseq2\");\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(1, rc);
    ASSERT_INT_EQ(1, output_contains(out, "sequence does not exist"));
}

TEST(phase12_sqlite_row_trigger_insert_fires_per_row) {
    remove("/tmp/test_phase12.db");
    char out[512];
    int rc = run_mypl_sqlite(
        "trigger strg_r_ins after insert on strg_ri for each row {\n"
        "    print \"s-row-ins\";\n"
        "    print :new.id;\n"
        "    print :new.name;\n"
        "}\n"
        "proc main() -> int {\n"
        "    create table strg_ri (id int, name string);\n"
        "    insert into strg_ri values (1, \"alice\");\n"
        "    insert into strg_ri values (2, \"bob\");\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(2, count_occurrences(out, "s-row-ins"));
    ASSERT_INT_EQ(1, count_occurrences(out, "alice"));
    ASSERT_INT_EQ(1, count_occurrences(out, "bob"));
    remove("/tmp/test_phase12.db");
}

TEST(phase12_sqlite_row_trigger_update_and_delete) {
    remove("/tmp/test_phase12.db");
    char out[512];
    int rc = run_mypl_sqlite(
        "trigger strg_r_upd after update on strg_ru for each row {\n"
        "    print \"s-upd\";\n"
        "    print :old.qty;\n"
        "    print :new.qty;\n"
        "}\n"
        "trigger strg_r_del after delete on strg_ru for each row {\n"
        "    print \"s-del\";\n"
        "    print :old.id;\n"
        "}\n"
        "proc main() -> int {\n"
        "    create table strg_ru (id int, qty int);\n"
        "    insert into strg_ru values (1, 10);\n"
        "    insert into strg_ru values (2, 20);\n"
        "    update strg_ru set qty = 99 where id = 2;\n"
        "    delete from strg_ru where id = 1;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, count_occurrences(out, "s-upd"));
    ASSERT_INT_EQ(1, count_occurrences(out, "s-del"));
    /* update: 20 -> 99; delete: id 1 */
    ASSERT_INT_EQ(1, count_occurrences(out, "20"));
    ASSERT_INT_EQ(1, count_occurrences(out, "99"));
    remove("/tmp/test_phase12.db");
}

TEST(phase12_sqlite_row_trigger_persists_across_restarts) {
    remove("/tmp/test_phase12.db");
    char out[512];
    int rc = run_mypl_sqlite(
        "trigger strg_r_per after insert on strg_rp for each row {\n"
        "    print \"s-row-persist-fired\";\n"
        "    print :new.id;\n"
        "}\n"
        "proc main() -> int {\n"
        "    create table strg_rp (id int);\n"
        "    insert into strg_rp values (1);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, count_occurrences(out, "s-row-persist-fired"));

    rc = run_mypl_sqlite(
        "proc main() -> int {\n"
        "    insert into strg_rp values (2);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, count_occurrences(out, "s-row-persist-fired"));
    ASSERT_INT_EQ(1, count_occurrences(out, "2"));
    remove("/tmp/test_phase12.db");
}

TEST(phase12_dbms_sql_sqlite_bind_and_execute) {
    remove("/tmp/test_phase12.db");
    char out[512];
    int rc = run_mypl_sqlite(
        "proc main() -> int {\n"
        "    create table dbms_t (id int, name string);\n"
        "    int c = dbms_sql.open_cursor();\n"
        "    dbms_sql.parse(c, \"insert into dbms_t values (?1, ?2)\");\n"
        "    dbms_sql.bind_variable(c, \"1\", 42);\n"
        "    dbms_sql.bind_variable(c, \"2\", \"dave\");\n"
        "    int n = dbms_sql.execute_cursor(c);\n"
        "    print n;\n"
        "    dbms_sql.parse(c, \"select id, name from dbms_t where id = ?1\");\n"
        "    dbms_sql.bind_variable(c, \"1\", 42);\n"
        "    dbms_sql.execute_cursor(c);\n"
        "    array<row> rows = dbms_sql.fetch_rows(c, 10);\n"
        "    print length(rows);\n"
        "    print rows[0].id;\n"
        "    print rows[0].name;\n"
        "    dbms_sql.close_cursor(c);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "1"));
    ASSERT_INT_EQ(1, output_contains(out, "42"));
    ASSERT_INT_EQ(1, output_contains(out, "dave"));
    remove("/tmp/test_phase12.db");
}

TEST(phase12_dbms_sql_sqlite_bind_string_with_quote) {
    remove("/tmp/test_phase12.db");
    char out[512];
    int rc = run_mypl_sqlite(
        "proc main() -> int {\n"
        "    create table dbms_t (id int, name string);\n"
        "    int c = dbms_sql.open_cursor();\n"
        "    dbms_sql.parse(c, \"insert into dbms_t values (?1, ?2)\");\n"
        "    dbms_sql.bind_variable(c, \"1\", 1);\n"
        "    dbms_sql.bind_variable(c, \"2\", \"o'brien\");\n"
        "    dbms_sql.execute_cursor(c);\n"
        "    dbms_sql.parse(c, \"select name from dbms_t\");\n"
        "    dbms_sql.execute_cursor(c);\n"
        "    array<row> rows = dbms_sql.fetch_rows(c, 1);\n"
        "    print rows[0].name;\n"
        "    dbms_sql.close_cursor(c);\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "o'brien"));
    remove("/tmp/test_phase12.db");
}
#endif

int main(void) {
    printf("test_phase12:\n");
    RUN_TEST(phase12_nextval_persists_across_restarts);
    RUN_TEST(phase12_currval_after_restart);
    RUN_TEST(phase12_drop_sequence_persists);
    RUN_TEST(phase12_currval_before_nextval_errors);
    RUN_TEST(phase12_sequence_survives_other_catalog_writes);
    RUN_TEST(phase12_duplicate_create_after_restart_errors);
    RUN_TEST(phase12_missing_sequence_errors_unchanged);
    RUN_TEST(phase12_row_trigger_insert_fires_per_row);
    RUN_TEST(phase12_row_trigger_update_sees_old_and_new);
    RUN_TEST(phase12_row_trigger_delete_sees_old);
    RUN_TEST(phase12_row_and_statement_triggers_both_fire);
    RUN_TEST(phase12_row_trigger_persists_across_restarts);
    RUN_TEST(phase12_row_trigger_fires_on_dynamic_sql);
    RUN_TEST(phase12_row_trigger_wrong_context_errors);
    RUN_TEST(phase12_row_trigger_requires_dml_event);
    RUN_TEST(phase12_dbms_sql_open_parse_execute_dml);
    RUN_TEST(phase12_dbms_sql_fetch_rows);
    RUN_TEST(phase12_dbms_sql_column_value);
    RUN_TEST(phase12_dbms_sql_close_invalidates_handle);
    RUN_TEST(phase12_external_call_f_return);
    RUN_TEST(phase12_external_call_int_return_from_float_and_string);
    RUN_TEST(phase12_external_call_s_return);
    RUN_TEST(phase12_external_call_s_result_is_copied);
    RUN_TEST(phase12_external_call_s_null_return_is_null);
    RUN_TEST(phase12_external_call_rejects_bool_argument);
    RUN_TEST(phase12_external_call_s_result_type_is_checked);
    RUN_TEST(phase12_external_call_s_missing_symbol_fails);
#ifdef USE_SQLITE
    RUN_TEST(phase12_dbms_sql_sqlite_bind_and_execute);
    RUN_TEST(phase12_dbms_sql_sqlite_bind_string_with_quote);
    RUN_TEST(phase12_sqlite_nextval_persists_across_restarts);
    RUN_TEST(phase12_sqlite_drop_sequence_persists);
    RUN_TEST(phase12_trigger_static_fires);
    RUN_TEST(phase12_trigger_persists_across_restarts);
    RUN_TEST(phase12_drop_trigger_stops_and_persists);
    RUN_TEST(phase12_trigger_fires_on_execute_immediate);
    RUN_TEST(phase12_trigger_fires_on_dbms_sql_execute);
    RUN_TEST(phase12_drop_trigger_via_execute_immediate);
    RUN_TEST(phase12_sqlite_trigger_persists_across_restarts);
    RUN_TEST(phase12_sqlite_drop_trigger_persists);
    RUN_TEST(phase12_sqlite_row_trigger_insert_fires_per_row);
    RUN_TEST(phase12_sqlite_row_trigger_update_and_delete);
    RUN_TEST(phase12_sqlite_row_trigger_persists_across_restarts);
#endif
    TEST_SUMMARY();
}
