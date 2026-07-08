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

TEST(sql_update_modifies_rows) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE users (id INT, age INT)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (1, 20)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (2, 30)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("UPDATE users SET age = 21 WHERE id = 1", &ctx));

    Result* res = sql_exec("SELECT age FROM users WHERE id = 1", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(1, res->row_count);
    Row* row = result_next(res);
    ASSERT_INT_EQ(21, row_get_field(row, "age").as.as_int);
    result_free(res);

    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_update_with_string) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE users (id INT, name STRING)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (1, 'alice')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("UPDATE users SET name = 'alicia' WHERE id = 1", &ctx));

    Result* res = sql_exec("SELECT name FROM users WHERE id = 1", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(1, res->row_count);
    Row* row = result_next(res);
    ASSERT_STRING_EQ("alicia", row_get_field(row, "name").as.as_string);
    result_free(res);

    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_delete_removes_rows) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE users (id INT, name STRING)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (1, 'alice')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (2, 'bob')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("DELETE FROM users WHERE id = 1", &ctx));

    Result* res = sql_exec("SELECT id FROM users", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(1, res->row_count);
    Row* row = result_next(res);
    ASSERT_INT_EQ(2, row_get_field(row, "id").as.as_int);
    result_free(res);

    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_update_and_delete_persist) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE users (id INT, name STRING, age INT)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (1, 'alice', 30)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (2, 'bob', 25)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("UPDATE users SET age = 31 WHERE id = 1", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("DELETE FROM users WHERE id = 2", &ctx));
    catalog_close(&ctx);

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    Result* res = sql_exec("SELECT id, name, age FROM users", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(1, res->row_count);
    Row* row = result_next(res);
    ASSERT_INT_EQ(1, row_get_field(row, "id").as.as_int);
    ASSERT_STRING_EQ("alice", row_get_field(row, "name").as.as_string);
    ASSERT_INT_EQ(31, row_get_field(row, "age").as.as_int);
    result_free(res);
    catalog_close(&ctx);

    cleanup(path);
}

TEST(sql_order_by_asc) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE users (id INT, name STRING)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (2, 'bob')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (1, 'alice')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (3, 'carol')", &ctx));

    Result* res = sql_exec("SELECT id, name FROM users ORDER BY id ASC", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(3, res->row_count);
    ASSERT_INT_EQ(1, row_get_field(&res->rows[0], "id").as.as_int);
    ASSERT_INT_EQ(2, row_get_field(&res->rows[1], "id").as.as_int);
    ASSERT_INT_EQ(3, row_get_field(&res->rows[2], "id").as.as_int);
    result_free(res);

    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_order_by_desc) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE users (id INT, name STRING)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (1, 'alice')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (3, 'carol')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (2, 'bob')", &ctx));

    Result* res = sql_exec("SELECT id FROM users ORDER BY id DESC", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(3, res->row_count);
    ASSERT_INT_EQ(3, row_get_field(&res->rows[0], "id").as.as_int);
    ASSERT_INT_EQ(2, row_get_field(&res->rows[1], "id").as.as_int);
    ASSERT_INT_EQ(1, row_get_field(&res->rows[2], "id").as.as_int);
    result_free(res);

    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_limit_clause) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE users (id INT, name STRING)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (1, 'alice')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (2, 'bob')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (3, 'carol')", &ctx));

    Result* res = sql_exec("SELECT id FROM users ORDER BY id DESC LIMIT 2", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(2, res->row_count);
    ASSERT_INT_EQ(3, row_get_field(&res->rows[0], "id").as.as_int);
    ASSERT_INT_EQ(2, row_get_field(&res->rows[1], "id").as.as_int);
    result_free(res);

    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_aggregate_count) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE users (id INT, name STRING)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (1, 'alice')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (2, 'bob')", &ctx));

    Result* res = sql_exec("SELECT COUNT(*) FROM users", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(1, res->row_count);
    ASSERT_INT_EQ(2, row_get_field(&res->rows[0], "count").as.as_int);
    result_free(res);

    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_aggregate_sum_avg_min_max) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE scores (id INT, score INT)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO scores VALUES (1, 10)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO scores VALUES (2, 20)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO scores VALUES (3, 30)", &ctx));

    Result* res = sql_exec("SELECT SUM(score), AVG(score), MIN(score), MAX(score) FROM scores", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(1, res->row_count);
    ASSERT_FLOAT_EQ(60.0, row_get_field(&res->rows[0], "sum").as.as_float);
    ASSERT_FLOAT_EQ(20.0, row_get_field(&res->rows[0], "avg").as.as_float);
    ASSERT_INT_EQ(10, row_get_field(&res->rows[0], "min").as.as_int);
    ASSERT_INT_EQ(30, row_get_field(&res->rows[0], "max").as.as_int);
    result_free(res);

    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_aggregate_with_where) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE scores (id INT, score INT)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO scores VALUES (1, 10)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO scores VALUES (2, 20)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO scores VALUES (3, 30)", &ctx));

    Result* res = sql_exec("SELECT COUNT(*), SUM(score) FROM scores WHERE score > 10", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(1, res->row_count);
    ASSERT_INT_EQ(2, row_get_field(&res->rows[0], "count").as.as_int);
    ASSERT_FLOAT_EQ(50.0, row_get_field(&res->rows[0], "sum").as.as_float);
    result_free(res);

    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_join_basic) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE users (id INT, name STRING)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE orders (id INT, user_id INT, total INT)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (1, 'alice')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (2, 'bob')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO orders VALUES (101, 1, 50)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO orders VALUES (102, 1, 75)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO orders VALUES (103, 2, 30)", &ctx));

    Result* res = sql_exec("SELECT name, total FROM users JOIN orders ON users.id = orders.user_id ORDER BY total DESC", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(3, res->row_count);
    ASSERT_STRING_EQ("alice", row_get_field(&res->rows[0], "name").as.as_string);
    ASSERT_INT_EQ(75, row_get_field(&res->rows[0], "total").as.as_int);
    ASSERT_STRING_EQ("alice", row_get_field(&res->rows[1], "name").as.as_string);
    ASSERT_INT_EQ(50, row_get_field(&res->rows[1], "total").as.as_int);
    ASSERT_STRING_EQ("bob", row_get_field(&res->rows[2], "name").as.as_string);
    ASSERT_INT_EQ(30, row_get_field(&res->rows[2], "total").as.as_int);
    result_free(res);

    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_join_select_star) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE a (a_id INT, a_name STRING)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE b (b_id INT, a_id INT, b_val INT)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO a VALUES (1, 'one')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO a VALUES (2, 'two')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO b VALUES (10, 1, 100)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO b VALUES (20, 2, 200)", &ctx));

    Result* res = sql_exec("SELECT * FROM a JOIN b ON a.a_id = b.a_id", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(2, res->row_count);
    ASSERT_INT_EQ(5, res->rows[0].field_count);
    ASSERT_INT_EQ(1, row_get_field(&res->rows[0], "a_id").as.as_int);
    ASSERT_STRING_EQ("one", row_get_field(&res->rows[0], "a_name").as.as_string);
    ASSERT_INT_EQ(100, row_get_field(&res->rows[0], "b_val").as.as_int);
    result_free(res);

    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_join_with_where_limit) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE users (id INT, name STRING)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE items (id INT, user_id INT, price INT)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (1, 'alice')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (2, 'bob')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO items VALUES (1, 1, 10)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO items VALUES (2, 1, 20)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO items VALUES (3, 2, 5)", &ctx));

    Result* res = sql_exec("SELECT id, price FROM users JOIN items ON users.id = items.user_id WHERE name = 'alice' LIMIT 1", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(1, res->row_count);
    ASSERT_INT_EQ(1, row_get_field(&res->rows[0], "id").as.as_int);
    ASSERT_INT_EQ(10, row_get_field(&res->rows[0], "price").as.as_int);
    result_free(res);

    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_join_no_matches) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE users (id INT, name STRING)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE orders (id INT, user_id INT, total INT)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (1, 'alice')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO orders VALUES (101, 2, 50)", &ctx));

    Result* res = sql_exec("SELECT * FROM users JOIN orders ON users.id = orders.user_id", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(0, res->row_count);
    result_free(res);

    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_qualified_column_select) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE people (id INT, name STRING, age INT)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO people VALUES (1, 'alice', 30)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO people VALUES (2, 'bob', 25)", &ctx));

    Result* res = sql_exec("SELECT people.name, people.age FROM people", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(2, res->row_count);
    ASSERT_STRING_EQ("alice", row_get_field(&res->rows[0], "name").as.as_string);
    ASSERT_INT_EQ(30, row_get_field(&res->rows[0], "age").as.as_int);
    ASSERT_STRING_EQ("bob", row_get_field(&res->rows[1], "name").as.as_string);
    ASSERT_INT_EQ(25, row_get_field(&res->rows[1], "age").as.as_int);
    result_free(res);

    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_qualified_column_where) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE users (id INT, name STRING)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (1, 'alice')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (2, 'bob')", &ctx));

    Result* res = sql_exec("SELECT users.name FROM users WHERE users.id = 2", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(1, res->row_count);
    ASSERT_STRING_EQ("bob", row_get_field(&res->rows[0], "name").as.as_string);
    result_free(res);

    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_qualified_column_order_by) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE users (id INT, name STRING)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (2, 'bob')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (1, 'alice')", &ctx));

    Result* res = sql_exec("SELECT users.name FROM users ORDER BY users.id ASC", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(2, res->row_count);
    ASSERT_STRING_EQ("alice", row_get_field(&res->rows[0], "name").as.as_string);
    ASSERT_STRING_EQ("bob", row_get_field(&res->rows[1], "name").as.as_string);
    result_free(res);

    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_qualified_column_in_join) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE users (id INT, name STRING)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE orders (id INT, user_id INT, total INT)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (1, 'alice')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (2, 'bob')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO orders VALUES (101, 1, 50)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO orders VALUES (102, 1, 75)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO orders VALUES (103, 2, 30)", &ctx));

    Result* res = sql_exec(
        "SELECT users.name, orders.total FROM users JOIN orders ON users.id = orders.user_id "
        "WHERE users.name = 'alice' ORDER BY orders.total DESC", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(2, res->row_count);
    ASSERT_STRING_EQ("alice", row_get_field(&res->rows[0], "name").as.as_string);
    ASSERT_INT_EQ(75, row_get_field(&res->rows[0], "total").as.as_int);
    ASSERT_STRING_EQ("alice", row_get_field(&res->rows[1], "name").as.as_string);
    ASSERT_INT_EQ(50, row_get_field(&res->rows[1], "total").as.as_int);
    result_free(res);

    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_left_join_includes_unmatched_left) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE users (id INT, name STRING)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE orders (id INT, user_id INT, total INT)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (1, 'alice')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (2, 'bob')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO orders VALUES (101, 2, 30)", &ctx));

    Result* res = sql_exec(
        "SELECT users.name, orders.total FROM users LEFT JOIN orders ON users.id = orders.user_id "
        "ORDER BY users.id ASC", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(2, res->row_count);
    ASSERT_STRING_EQ("alice", row_get_field(&res->rows[0], "name").as.as_string);
    ASSERT_INT_EQ(0, row_get_field(&res->rows[0], "total").as.as_int);
    ASSERT_STRING_EQ("bob", row_get_field(&res->rows[1], "name").as.as_string);
    ASSERT_INT_EQ(30, row_get_field(&res->rows[1], "total").as.as_int);
    result_free(res);

    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_left_join_no_matches) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE users (id INT, name STRING)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE orders (id INT, user_id INT, total INT)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (1, 'alice')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO orders VALUES (101, 2, 50)", &ctx));

    Result* res = sql_exec(
        "SELECT users.name, orders.total FROM users LEFT JOIN orders ON users.id = orders.user_id", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(1, res->row_count);
    ASSERT_STRING_EQ("alice", row_get_field(&res->rows[0], "name").as.as_string);
    ASSERT_INT_EQ(0, row_get_field(&res->rows[0], "total").as.as_int);
    result_free(res);

    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_left_join_with_where) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE users (id INT, name STRING)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE orders (id INT, user_id INT, total INT)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (1, 'alice')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO users VALUES (2, 'bob')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO orders VALUES (101, 2, 30)", &ctx));

    Result* res = sql_exec(
        "SELECT users.name, orders.total FROM users LEFT JOIN orders ON users.id = orders.user_id "
        "WHERE users.id = 1", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(1, res->row_count);
    ASSERT_STRING_EQ("alice", row_get_field(&res->rows[0], "name").as.as_string);
    ASSERT_INT_EQ(0, row_get_field(&res->rows[0], "total").as.as_int);
    result_free(res);

    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_group_by_count_sum) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE scores (category INT, score INT)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO scores VALUES (1, 10)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO scores VALUES (1, 20)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO scores VALUES (2, 30)", &ctx));

    Result* res = sql_exec(
        "SELECT category, COUNT(*), SUM(score) FROM scores GROUP BY category ORDER BY category ASC", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(2, res->row_count);
    ASSERT_INT_EQ(1, row_get_field(&res->rows[0], "category").as.as_int);
    ASSERT_INT_EQ(2, row_get_field(&res->rows[0], "count").as.as_int);
    ASSERT_FLOAT_EQ(30.0, row_get_field(&res->rows[0], "sum").as.as_float);
    ASSERT_INT_EQ(2, row_get_field(&res->rows[1], "category").as.as_int);
    ASSERT_INT_EQ(1, row_get_field(&res->rows[1], "count").as.as_int);
    ASSERT_FLOAT_EQ(30.0, row_get_field(&res->rows[1], "sum").as.as_float);
    result_free(res);

    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_group_by_avg_min_max) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE scores (category INT, score INT)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO scores VALUES (1, 10)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO scores VALUES (1, 30)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO scores VALUES (2, 20)", &ctx));

    Result* res = sql_exec(
        "SELECT category, AVG(score), MIN(score), MAX(score) FROM scores GROUP BY category ORDER BY category ASC", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(2, res->row_count);
    ASSERT_INT_EQ(1, row_get_field(&res->rows[0], "category").as.as_int);
    ASSERT_FLOAT_EQ(20.0, row_get_field(&res->rows[0], "avg").as.as_float);
    ASSERT_INT_EQ(10, row_get_field(&res->rows[0], "min").as.as_int);
    ASSERT_INT_EQ(30, row_get_field(&res->rows[0], "max").as.as_int);
    ASSERT_INT_EQ(2, row_get_field(&res->rows[1], "category").as.as_int);
    ASSERT_FLOAT_EQ(20.0, row_get_field(&res->rows[1], "avg").as.as_float);
    ASSERT_INT_EQ(20, row_get_field(&res->rows[1], "min").as.as_int);
    ASSERT_INT_EQ(20, row_get_field(&res->rows[1], "max").as.as_int);
    result_free(res);

    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_group_by_with_where) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE scores (category INT, score INT)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO scores VALUES (1, 10)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO scores VALUES (1, 20)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO scores VALUES (2, 30)", &ctx));

    Result* res = sql_exec(
        "SELECT category, COUNT(*), SUM(score) FROM scores WHERE score > 10 GROUP BY category", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(2, res->row_count);
    result_free(res);

    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_insert_into_select_star) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE source (id INT, name STRING)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE target (id INT, name STRING)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO source VALUES (1, 'alice')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO source VALUES (2, 'bob')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO target SELECT * FROM source", &ctx));

    Result* res = sql_exec("SELECT id, name FROM target ORDER BY id ASC", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(2, res->row_count);
    ASSERT_INT_EQ(1, row_get_field(&res->rows[0], "id").as.as_int);
    ASSERT_STRING_EQ("alice", row_get_field(&res->rows[0], "name").as.as_string);
    ASSERT_INT_EQ(2, row_get_field(&res->rows[1], "id").as.as_int);
    ASSERT_STRING_EQ("bob", row_get_field(&res->rows[1], "name").as.as_string);
    result_free(res);

    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_insert_into_select_specific_columns) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE source (id INT, name STRING, age INT)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE target (id INT, name STRING)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO source VALUES (1, 'alice', 30)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO source VALUES (2, 'bob', 25)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO target SELECT id, name FROM source", &ctx));

    Result* res = sql_exec("SELECT id, name FROM target ORDER BY id ASC", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(2, res->row_count);
    ASSERT_INT_EQ(1, row_get_field(&res->rows[0], "id").as.as_int);
    ASSERT_STRING_EQ("alice", row_get_field(&res->rows[0], "name").as.as_string);
    ASSERT_INT_EQ(2, row_get_field(&res->rows[1], "id").as.as_int);
    ASSERT_STRING_EQ("bob", row_get_field(&res->rows[1], "name").as.as_string);
    result_free(res);

    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_insert_into_select_with_where) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE source (id INT, score INT)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE target (id INT, score INT)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO source VALUES (1, 10)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO source VALUES (2, 20)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO source VALUES (3, 30)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO target SELECT id, score FROM source WHERE score > 10", &ctx));

    Result* res = sql_exec("SELECT id, score FROM target ORDER BY id ASC", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(2, res->row_count);
    ASSERT_INT_EQ(2, row_get_field(&res->rows[0], "id").as.as_int);
    ASSERT_INT_EQ(20, row_get_field(&res->rows[0], "score").as.as_int);
    ASSERT_INT_EQ(3, row_get_field(&res->rows[1], "id").as.as_int);
    ASSERT_INT_EQ(30, row_get_field(&res->rows[1], "score").as.as_int);
    result_free(res);

    catalog_close(&ctx);
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
    RUN_TEST(sql_update_modifies_rows);
    RUN_TEST(sql_update_with_string);
    RUN_TEST(sql_delete_removes_rows);
    RUN_TEST(sql_update_and_delete_persist);
    RUN_TEST(sql_order_by_asc);
    RUN_TEST(sql_order_by_desc);
    RUN_TEST(sql_limit_clause);
    RUN_TEST(sql_aggregate_count);
    RUN_TEST(sql_aggregate_sum_avg_min_max);
    RUN_TEST(sql_aggregate_with_where);
    RUN_TEST(sql_join_basic);
    RUN_TEST(sql_join_select_star);
    RUN_TEST(sql_join_with_where_limit);
    RUN_TEST(sql_join_no_matches);
    RUN_TEST(sql_qualified_column_select);
    RUN_TEST(sql_qualified_column_where);
    RUN_TEST(sql_qualified_column_order_by);
    RUN_TEST(sql_qualified_column_in_join);
    RUN_TEST(sql_left_join_includes_unmatched_left);
    RUN_TEST(sql_left_join_no_matches);
    RUN_TEST(sql_left_join_with_where);
    RUN_TEST(sql_group_by_count_sum);
    RUN_TEST(sql_group_by_avg_min_max);
    RUN_TEST(sql_group_by_with_where);
    RUN_TEST(sql_insert_into_select_star);
    RUN_TEST(sql_insert_into_select_specific_columns);
    RUN_TEST(sql_insert_into_select_with_where);
    TEST_SUMMARY();
}
