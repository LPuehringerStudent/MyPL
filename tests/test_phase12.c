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

static void clean_trigger_db(void) {
    remove("mypl.db");
    remove("mypl.db.programs");
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
#ifdef USE_SQLITE
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
#endif
    TEST_SUMMARY();
}
