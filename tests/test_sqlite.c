#include "test_harness.h"
#include <sqlite3.h>
#include "sql_engine.h"
#include "compiler.h"
#include "sqlite_driver.h"
#include "vm.h"

TEST(sqlite3_is_linked) {
    sqlite3* db = NULL;
    int rc = sqlite3_open(":memory:", &db);
    ASSERT_INT_EQ(SQLITE_OK, rc);
    sqlite3_close(db);
}

TEST(sqlite_driver_crud) {
    DBDriver driver;
    sqlite_driver_init(&driver);
    int rc = driver.open(&driver, ":memory:");
    ASSERT_INT_EQ(1, rc);
    rc = driver.exec(&driver, "CREATE TABLE t (id INTEGER, name TEXT)", NULL, 0);
    ASSERT_INT_EQ(1, rc);
    rc = driver.exec(&driver, "INSERT INTO t VALUES (1, 'alice')", NULL, 0);
    ASSERT_INT_EQ(1, rc);

    void* result = NULL;
    rc = driver.query(&driver, "SELECT id, name FROM t", NULL, 0, &result);
    ASSERT_INT_EQ(1, rc);

    void* row = NULL;
    rc = driver.result_next(&driver, result, &row);
    ASSERT_INT_EQ(1, rc);

    Value id;
    rc = driver.row_get_field(&driver, row, "id", &id);
    ASSERT_INT_EQ(1, rc);
    ASSERT_INT_EQ(VAL_INT, id.type);
    ASSERT_INT_EQ(1, id.as.as_int);

    Value name;
    rc = driver.row_get_field(&driver, row, "name", &name);
    ASSERT_INT_EQ(1, rc);
    ASSERT_INT_EQ(VAL_STRING, name.type);
    ASSERT_STRING_EQ("alice", name.as.as_string);

    driver.result_free(&driver, result);
    driver.close(&driver);
}

TEST(sqlite_driver_parameter_binding) {
    DBDriver driver;
    sqlite_driver_init(&driver);
    ASSERT_INT_EQ(1, driver.open(&driver, ":memory:"));
    ASSERT_INT_EQ(1, driver.exec(&driver, "CREATE TABLE t (id INTEGER, name TEXT)", NULL, 0));

    Value params[2];
    params[0] = value_int(42);
    params[1] = value_string(strdup("bob"));
    ASSERT_INT_EQ(1, driver.exec(&driver, "INSERT INTO t VALUES (?, ?)", params, 2));

    Value qparams[1];
    qparams[0] = value_int(42);
    void* result = NULL;
    ASSERT_INT_EQ(1, driver.query(&driver, "SELECT id, name FROM t WHERE id = ?", qparams, 1, &result));

    void* row = NULL;
    ASSERT_INT_EQ(1, driver.result_next(&driver, result, &row));

    Value name;
    ASSERT_INT_EQ(1, driver.row_get_field(&driver, row, "name", &name));
    ASSERT_STRING_EQ("bob", name.as.as_string);

    value_release(params[1]);
    driver.result_free(&driver, result);
    driver.close(&driver);
}

TEST(sqlite_end_to_end_ddl_and_dml) {
    DBDriver driver;
    sqlite_driver_init(&driver);
    ASSERT_INT_EQ(1, driver.open(&driver, ":memory:"));

    Chunk chunk;
    init_chunk(&chunk);
    char error[256];
    int ok = compile_with_context_and_path(
        "proc main() -> int { create table t (id int); insert into t values (1); return 0; }",
        &chunk, NULL, error, sizeof(error), NULL);
    ASSERT_INT_EQ(1, ok);

    VM* vm = vm_init();
    vm_set_driver(vm, &driver);
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));

    vm_free(vm);
    free_chunk(&chunk);
    driver.close(&driver);
}

TEST(sqlite_end_to_end_parameter_binding) {
    DBDriver driver;
    sqlite_driver_init(&driver);
    ASSERT_INT_EQ(1, driver.open(&driver, ":memory:"));

    Chunk chunk;
    init_chunk(&chunk);
    char error[256];
    int ok = compile_with_context_and_path(
        "proc main() -> int { string name = \"alice\"; create table t (id int, name string); insert into t values (1, ?name); return 0; }",
        &chunk, NULL, error, sizeof(error), NULL);
    ASSERT_INT_EQ(1, ok);

    VM* vm = vm_init();
    vm_set_driver(vm, &driver);
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));

    void* result = NULL;
    ASSERT_INT_EQ(1, driver.query(&driver, "SELECT name FROM t WHERE id = 1", NULL, 0, &result));
    void* row = NULL;
    ASSERT_INT_EQ(1, driver.result_next(&driver, result, &row));
    Value name;
    ASSERT_INT_EQ(1, driver.row_get_field(&driver, row, "name", &name));
    ASSERT_INT_EQ(VAL_STRING, name.type);
    ASSERT_STRING_EQ("alice", name.as.as_string);
    value_release(name);
    driver.result_free(&driver, result);

    vm_free(vm);
    free_chunk(&chunk);
    driver.close(&driver);
}

int main(void) {
    RUN_TEST(sqlite3_is_linked);
    RUN_TEST(sqlite_driver_crud);
    RUN_TEST(sqlite_driver_parameter_binding);
    RUN_TEST(sqlite_end_to_end_ddl_and_dml);
    RUN_TEST(sqlite_end_to_end_parameter_binding);
    TEST_SUMMARY();
}
