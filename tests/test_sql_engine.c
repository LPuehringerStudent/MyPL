#include "test_harness.h"
#include "sql_engine.h"
#include "compiler.h"
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char* make_temp_path(void) {
    char* path = malloc(256);
    if (path == NULL) return NULL;
    snprintf(path, 256, "/tmp/mydb_test_sql_%d_%d.db", (int)getpid(), (int)(size_t)path);
    if (strlen(path) > 240) path[240] = '\0';
    unlink(path);
    return path;
}

static void cleanup(char* path) {
    if (path == NULL) return;
    unlink(path);
    free(path);
}

TEST(sql_create_table_persists_schema) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE users (id INT, name STRING)", &ctx));
    catalog_close(&ctx);

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    Result* res = sql_exec("SELECT id, name FROM users", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(0, res->row_count);
    result_free(res);
    catalog_close(&ctx);

    cleanup(path);
}

TEST(sql_insert_and_select_persists_rows) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE users (id INT, name STRING)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (1, 'alice')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (2, 'bob')", &ctx));
    catalog_close(&ctx);

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    Result* res = sql_exec("SELECT * FROM users", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(2, res->row_count);

    Row* row = result_next(res);
    ASSERT_PTR_NOT_NULL(row);
    ASSERT_INT_EQ(2, row->field_count);
    ASSERT_INT_EQ(1, row_get_field(row, "id").as.as_int);
    ASSERT_STRING_EQ("alice", row_get_field(row, "name").as.as_string);

    row = result_next(res);
    ASSERT_PTR_NOT_NULL(row);
    ASSERT_INT_EQ(2, row->field_count);
    ASSERT_INT_EQ(2, row_get_field(row, "id").as.as_int);
    ASSERT_STRING_EQ("bob", row_get_field(row, "name").as.as_string);

    result_free(res);
    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_select_specific_columns) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE people (id INT, name STRING, age INT)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO people VALUES (1, 'alice', 30)", &ctx));

    Result* res = sql_exec("SELECT name, age FROM people", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(1, res->row_count);

    Row* row = result_next(res);
    ASSERT_PTR_NOT_NULL(row);
    ASSERT_INT_EQ(2, row->field_count);
    ASSERT_STRING_EQ("alice", row_get_field(row, "name").as.as_string);
    ASSERT_INT_EQ(30, row_get_field(row, "age").as.as_int);

    result_free(res);
    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_where_equals_int) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE users (id INT, name STRING)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (1, 'alice')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (2, 'bob')", &ctx));

    Result* res = sql_exec("SELECT name FROM users WHERE id = 2", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(1, res->row_count);

    Row* row = result_next(res);
    ASSERT_STRING_EQ("bob", row_get_field(row, "name").as.as_string);

    result_free(res);
    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_where_greater_than_int) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE people (id INT, age INT)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO people VALUES (1, 20)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO people VALUES (2, 35)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO people VALUES (3, 40)", &ctx));

    Result* res = sql_exec("SELECT id FROM people WHERE age > 25", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(2, res->row_count);

    Row* row = result_next(res);
    ASSERT_INT_EQ(2, row_get_field(row, "id").as.as_int);
    row = result_next(res);
    ASSERT_INT_EQ(3, row_get_field(row, "id").as.as_int);

    result_free(res);
    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_select_unknown_table_returns_empty_result) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    Result* res = sql_exec("SELECT id FROM missing", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(0, res->row_count);
    result_free(res);
    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_compiler_loop_uses_persisted_table) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE users (id INT)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (7)", &ctx));
    catalog_close(&ctx);

    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { for row in SELECT id FROM users { return row.id; } return 0; }", &chunk, NULL, 0));

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    Result* check = sql_exec("SELECT id FROM users", &ctx);
    ASSERT_PTR_NOT_NULL(check);
    ASSERT_INT_EQ(1, check->row_count);
    result_free(check);

    VM* vm = vm_init();
    vm_set_context(vm, &ctx);
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(7, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
    catalog_close(&ctx);
    cleanup(path);
}

TEST(custom_driver_runs_create_and_insert) {
    char* path = make_temp_path();
    DBDriver driver;
    custom_driver_init(&driver);
    int rc = driver.open(&driver, path);
    ASSERT_INT_EQ(1, rc);
    rc = driver.exec(&driver, "CREATE TABLE users (id INT, name STRING)", NULL, 0);
    ASSERT_INT_EQ(1, rc);
    rc = driver.exec(&driver, "INSERT INTO users VALUES (1, 'alice')", NULL, 0);
    ASSERT_INT_EQ(1, rc);

    void* result = NULL;
    rc = driver.query(&driver, "SELECT id, name FROM users", NULL, 0, &result);
    ASSERT_INT_EQ(1, rc);

    void* row = NULL;
    rc = driver.result_next(&driver, result, &row);
    ASSERT_INT_EQ(1, rc);

    Value id;
    rc = driver.row_get_field(&driver, row, "id", &id);
    ASSERT_INT_EQ(1, rc);
    ASSERT_INT_EQ(VAL_INT, id.type);
    ASSERT_INT_EQ(1, id.as.as_int);

    driver.result_free(&driver, result);
    driver.close(&driver);
    cleanup(path);
}

int main(void) {
    RUN_TEST(sql_create_table_persists_schema);
    RUN_TEST(sql_insert_and_select_persists_rows);
    RUN_TEST(sql_select_specific_columns);
    RUN_TEST(sql_where_equals_int);
    RUN_TEST(sql_where_greater_than_int);
    RUN_TEST(sql_select_unknown_table_returns_empty_result);
    RUN_TEST(sql_compiler_loop_uses_persisted_table);
    RUN_TEST(custom_driver_runs_create_and_insert);
    TEST_SUMMARY();
}
