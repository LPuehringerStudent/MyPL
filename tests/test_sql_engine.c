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

/* Row-page chain (issue #50): a page's next_page link shares header bytes
 * with the old record-count field, so a full page that has a successor used
 * to read back as empty. These tables are sized to span many pages: each row
 * is ~112 bytes, ~36 rows per 4 KB page. */
#define BIG_ROWS 300

static int insert_big_rows(Context* ctx, int first_id, int last_id) {
    char pad[101];
    memset(pad, 'x', 100);
    pad[100] = '\0';
    for (int id = first_id; id <= last_id; id++) {
        char sql[256];
        snprintf(sql, sizeof(sql), "INSERT INTO big VALUES (%d, '%s')", id, pad);
        if (!sql_exec_ddl(sql, ctx)) return 0;
    }
    return 1;
}

static int select_count(Context* ctx, const char* sql) {
    Result* res = sql_exec(sql, ctx);
    if (res == NULL) return -1;
    int n = res->row_count;
    result_free(res);
    return n;
}

static int select_single_int(Context* ctx, const char* sql, const char* field) {
    Result* res = sql_exec(sql, ctx);
    if (res == NULL || res->row_count != 1) {
        if (res != NULL) result_free(res);
        return -1;
    }
    int v = row_get_field(&res->rows[0], field).as.as_int;
    result_free(res);
    return v;
}

static double select_single_float(Context* ctx, const char* sql, const char* field) {
    Result* res = sql_exec(sql, ctx);
    if (res == NULL || res->row_count != 1) {
        if (res != NULL) result_free(res);
        return -1.0;
    }
    double v = row_get_field(&res->rows[0], field).as.as_float;
    result_free(res);
    return v;
}

TEST(sql_scan_returns_rows_from_full_middle_pages) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE big (id INT, pad STRING)", &ctx));
    ASSERT_INT_EQ(1, insert_big_rows(&ctx, 1, BIG_ROWS));

    ASSERT_INT_EQ(BIG_ROWS, select_count(&ctx, "SELECT * FROM big"));
    ASSERT_INT_EQ(BIG_ROWS, select_single_int(&ctx, "SELECT COUNT(*) FROM big", "count"));
    ASSERT_FLOAT_EQ((double)(BIG_ROWS * (BIG_ROWS + 1) / 2),
                    select_single_float(&ctx, "SELECT SUM(id) FROM big", "sum"));
    /* A row that lives on a full middle page. */
    ASSERT_INT_EQ(1, select_count(&ctx, "SELECT id FROM big WHERE id = 100"));

    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_index_build_sees_rows_on_full_middle_pages) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE big (id INT, pad STRING)", &ctx));
    ASSERT_INT_EQ(1, insert_big_rows(&ctx, 1, BIG_ROWS));
    /* Built after the rows exist, so it has to walk the whole chain. */
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE INDEX big_id ON big (id)", &ctx));

    for (int id = 1; id <= BIG_ROWS; id += 37) {
        char sql[128];
        snprintf(sql, sizeof(sql), "SELECT id FROM big WHERE id = %d", id);
        ASSERT_INT_EQ(1, select_count(&ctx, sql));
    }
    ASSERT_INT_EQ(1, select_count(&ctx, "SELECT id FROM big WHERE id = 300"));
    ASSERT_INT_EQ(BIG_ROWS, select_count(&ctx, "SELECT * FROM big"));

    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_reads_and_appends_to_legacy_last_page_with_record_count) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE big (id INT, pad STRING)", &ctx));
    ASSERT_INT_EQ(1, insert_big_rows(&ctx, 1, BIG_ROWS));
    catalog_close(&ctx);

    /* Databases written before the fix kept a record count at header offset
       2 on the last page, which is the high half of its next_page field: the
       last page therefore "linked" to page count<<16, far past the end of the
       file. Recreate that state and require scans and appends to cope. */
    ASSERT_INT_EQ(1, catalog_open(&ctx));
    Table* t = catalog_find_table(&ctx, "big");
    ASSERT_PTR_NOT_NULL(t);
    int page_num = t->first_row_page;
    int pages = 0;
    for (;;) {
        uint8_t page[PAGE_SIZE];
        pager_read_page(ctx.pager, page_num, page);
        int32_t next;
        memcpy(&next, page, sizeof(next));
        pages++;
        if (next == 0) {
            int16_t legacy_count = 5;
            memcpy(page + 2, &legacy_count, sizeof(legacy_count));
            pager_write_page(ctx.pager, page_num, page);
            break;
        }
        page_num = (int)next;
    }
    ASSERT(pages > 2);
    catalog_close(&ctx);

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(BIG_ROWS, select_count(&ctx, "SELECT * FROM big"));
    ASSERT_INT_EQ(1, insert_big_rows(&ctx, BIG_ROWS + 1, BIG_ROWS + 1));
    ASSERT_INT_EQ(BIG_ROWS + 1, select_count(&ctx, "SELECT * FROM big"));
    ASSERT_INT_EQ(1, sql_exec_ddl("DROP TABLE big", &ctx));
    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_insert_after_reopen_keeps_multi_page_chain) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE big (id INT, pad STRING)", &ctx));
    ASSERT_INT_EQ(1, insert_big_rows(&ctx, 1, BIG_ROWS));
    catalog_close(&ctx);

    /* New process equivalent: last_row_page is not persisted, so appends must
       find the real tail instead of treating the first page as the tail. */
    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, insert_big_rows(&ctx, BIG_ROWS + 1, 2 * BIG_ROWS));
    ASSERT_INT_EQ(2 * BIG_ROWS, select_count(&ctx, "SELECT * FROM big"));
    catalog_close(&ctx);

    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(2 * BIG_ROWS, select_count(&ctx, "SELECT * FROM big"));
    ASSERT_INT_EQ(1, select_count(&ctx, "SELECT id FROM big WHERE id = 450"));
    catalog_close(&ctx);
    cleanup(path);
}

/* ---- Bind parameters (?) ---------------------------------------------- */

static int bound_row_count(Context* ctx, const char* query, const Value* params, int count) {
    Result* res = sql_exec_params(query, ctx, params, count);
    if (res == NULL) return -1;
    int rows = res->row_count;
    result_free(res);
    return rows;
}

TEST(sql_bound_insert_values_of_each_type) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};
    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE t (id INT, price FLOAT, name STRING)", &ctx));

    /* -5 has no literal spelling in the engine's SQL lexer, so a value that
       round-trips proves it is bound as a value and not pasted into the text. */
    Value first[3] = {value_int(-5), value_float(2.5), value_string(strdup("bob"))};
    ASSERT_INT_EQ(1, sql_exec_ddl_params("INSERT INTO t VALUES (?, ?, ?)", &ctx, first, 3));
    value_release(first[2]);

    Value second[3] = {value_int(2), value_null(), value_bool(1)};
    ASSERT_INT_EQ(1, sql_exec_ddl_params("INSERT INTO t VALUES (?, ?, ?)", &ctx, second, 3));

    Value find_first[1] = {value_int(-5)};
    Result* res = sql_exec_params("SELECT price, name FROM t WHERE id = ?", &ctx, find_first, 1);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(1, res->row_count);
    Row* row = result_next(res);
    ASSERT_FLOAT_EQ(2.5, row_get_field(row, "price").as.as_float);
    ASSERT_STRING_EQ("bob", row_get_field(row, "name").as.as_string);
    result_free(res);

    res = sql_exec("SELECT price FROM t WHERE id = 2", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(1, res->row_count);
    ASSERT_INT_EQ(VAL_NULL, row_get_field(result_next(res), "price").type);
    result_free(res);

    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_bound_where_update_delete) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};
    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE t (id INT, name STRING)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO t VALUES (1, 'a')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO t VALUES (2, 'b')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO t VALUES (3, 'c')", &ctx));

    Value two[1] = {value_int(2)};
    ASSERT_INT_EQ(1, bound_row_count(&ctx, "SELECT id FROM t WHERE id = ?", two, 1));
    Value one[1] = {value_int(1)};
    ASSERT_INT_EQ(2, bound_row_count(&ctx, "SELECT id FROM t WHERE id > ?", one, 1));

    Value upd[2] = {value_string(strdup("B")), value_int(2)};
    ASSERT_INT_EQ(1, sql_exec_ddl_params("UPDATE t SET name = ? WHERE id = ?", &ctx, upd, 2));
    value_release(upd[0]);
    Result* res = sql_exec("SELECT name FROM t WHERE id = 2", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_STRING_EQ("B", row_get_field(result_next(res), "name").as.as_string);
    result_free(res);

    Value del[1] = {value_int(3)};
    ASSERT_INT_EQ(1, sql_exec_ddl_params("DELETE FROM t WHERE id = ?", &ctx, del, 1));
    ASSERT_INT_EQ(2, bound_row_count(&ctx, "SELECT id FROM t", NULL, 0));

    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_bound_in_list_like_and_limit) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};
    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE t (id INT, name STRING)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO t VALUES (1, 'apple')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO t VALUES (2, 'apricot')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO t VALUES (3, 'banana')", &ctx));

    Value in[2] = {value_int(1), value_int(3)};
    ASSERT_INT_EQ(2, bound_row_count(&ctx, "SELECT id FROM t WHERE id IN (?, ?)", in, 2));

    Value like[1] = {value_string(strdup("ap%"))};
    ASSERT_INT_EQ(2, bound_row_count(&ctx, "SELECT id FROM t WHERE name LIKE ?", like, 1));
    value_release(like[0]);

    /* A LIKE pattern has to be text; a number is rejected, not coerced. */
    Value not_text[1] = {value_int(7)};
    ASSERT_INT_EQ(0, bound_row_count(&ctx, "SELECT id FROM t WHERE name LIKE ?", not_text, 1));

    Value limit[1] = {value_int(2)};
    ASSERT_INT_EQ(2, bound_row_count(&ctx, "SELECT id FROM t LIMIT ?", limit, 1));

    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_bound_string_is_data_not_sql) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};
    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE t (id INT, name STRING)", &ctx));

    /* Both quote characters, a placeholder look-alike and a statement
       terminator; none of it may be interpreted. */
    const char* nasty = "x' OR '1'='1\" ?2; DROP TABLE t";
    Value params[2] = {value_int(1), value_string(strdup(nasty))};
    ASSERT_INT_EQ(1, sql_exec_ddl_params("INSERT INTO t VALUES (?, ?)", &ctx, params, 2));
    value_release(params[1]);
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO t VALUES (2, 'other')", &ctx));

    Value find[1] = {value_string(strdup(nasty))};
    Result* res = sql_exec_params("SELECT id, name FROM t WHERE name = ?", &ctx, find, 1);
    value_release(find[0]);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(1, res->row_count);
    Row* row = result_next(res);
    ASSERT_INT_EQ(1, row_get_field(row, "id").as.as_int);
    ASSERT_STRING_EQ(nasty, row_get_field(row, "name").as.as_string);
    result_free(res);

    ASSERT_INT_EQ(2, bound_row_count(&ctx, "SELECT id FROM t", NULL, 0));
    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_bound_question_mark_in_literal_is_not_a_placeholder) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};
    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE t (id INT, note STRING)", &ctx));

    Value params[1] = {value_int(4)};
    ASSERT_INT_EQ(1, sql_exec_ddl_params("INSERT INTO t VALUES (?, 'why?')", &ctx, params, 1));
    Result* res = sql_exec("SELECT note FROM t WHERE id = 4", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_STRING_EQ("why?", row_get_field(result_next(res), "note").as.as_string);
    result_free(res);

    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_bound_placeholder_count_must_match) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};
    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE t (id INT)", &ctx));

    Value one[1] = {value_int(1)};
    Value two[2] = {value_int(1), value_int(2)};
    ASSERT_INT_EQ(0, sql_exec_ddl_params("INSERT INTO t VALUES (?)", &ctx, two, 2));
    ASSERT_INT_EQ(0, sql_exec_ddl_params("INSERT INTO t VALUES (?, ?)", &ctx, one, 1));
    ASSERT_PTR_NULL(sql_exec_params("SELECT id FROM t WHERE id = ? AND id = ?", &ctx, one, 1));
    ASSERT_INT_EQ(0, bound_row_count(&ctx, "SELECT id FROM t", NULL, 0));

    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_bound_insert_select_keeps_placeholder_positions) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};
    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE src (id INT, name STRING)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE dst (id INT, name STRING)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO src VALUES (1, 'a')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO src VALUES (2, 'b')", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO src VALUES (3, 'c')", &ctx));

    /* The SELECT is executed on its own, so its placeholders must still refer
       to the second and third values of the whole statement. */
    Value params[3] = {value_int(0), value_int(1), value_string(strdup("c"))};
    ASSERT_INT_EQ(1, sql_exec_ddl_params(
        "INSERT INTO dst SELECT id, name FROM src WHERE id > ? AND name <> ?", &ctx, params + 1, 2));
    value_release(params[2]);
    ASSERT_INT_EQ(1, bound_row_count(&ctx, "SELECT id FROM dst", NULL, 0));

    catalog_close(&ctx);
    cleanup(path);
}

TEST(sql_bound_view_definition_rejects_placeholders) {
    char* path = make_temp_path();
    Context ctx = {path, NULL};
    ASSERT_INT_EQ(1, catalog_open(&ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE TABLE t (id INT)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO t VALUES (1)", &ctx));
    ASSERT_INT_EQ(1, sql_exec_ddl("INSERT INTO t VALUES (2)", &ctx));

    Value one[1] = {value_int(1)};
    ASSERT_INT_EQ(0, sql_exec_ddl_params("CREATE VIEW v AS SELECT id FROM t WHERE id = ?", &ctx, one, 1));

    /* A view is unaffected by the values bound to the query that reads it. */
    ASSERT_INT_EQ(1, sql_exec_ddl("CREATE VIEW v2 AS SELECT id FROM t WHERE id > 1", &ctx));
    Value nine[1] = {value_int(9)};
    ASSERT_INT_EQ(1, bound_row_count(&ctx, "SELECT id FROM v2 WHERE id < ?", nine, 1));

    catalog_close(&ctx);
    cleanup(path);
}

TEST(custom_driver_binds_params_for_exec_and_query) {
    char* path = make_temp_path();
    DBDriver driver;
    custom_driver_init(&driver);
    ASSERT_INT_EQ(1, driver.open(&driver, path));
    ASSERT_INT_EQ(1, driver.exec(&driver, "CREATE TABLE users (id INT, name STRING)", NULL, 0));

    Value ins[2] = {value_int(7), value_string(strdup("o'brien"))};
    ASSERT_INT_EQ(1, driver.exec(&driver, "INSERT INTO users VALUES (?, ?)", ins, 2));
    value_release(ins[1]);

    Value find[1] = {value_int(7)};
    void* result = NULL;
    ASSERT_INT_EQ(1, driver.query(&driver, "SELECT name FROM users WHERE id = ?", find, 1, &result));
    void* row = NULL;
    ASSERT_INT_EQ(1, driver.result_next(&driver, result, &row));
    Value name;
    ASSERT_INT_EQ(1, driver.row_get_field(&driver, row, "name", &name));
    ASSERT_STRING_EQ("o'brien", name.as.as_string);
    value_release(name);
    driver.result_free(&driver, result);

    driver.close(&driver);
    cleanup(path);
}

TEST(custom_driver_reports_placeholder_count_mismatch) {
    char* path = make_temp_path();
    DBDriver driver;
    custom_driver_init(&driver);
    ASSERT_INT_EQ(1, driver.open(&driver, path));
    ASSERT_INT_EQ(1, driver.exec(&driver, "CREATE TABLE users (id INT)", NULL, 0));

    Value two[2] = {value_int(1), value_int(2)};
    ASSERT_INT_EQ(-1, driver.exec(&driver, "INSERT INTO users VALUES (?)", two, 2));
    ASSERT_INT_EQ(1, strstr(driver.error_message, "1 bind placeholder(s) but 2 value(s)") != NULL);

    void* result = NULL;
    Value one[1] = {value_int(1)};
    ASSERT_INT_EQ(0, driver.query(&driver, "SELECT id FROM users WHERE id = ? AND id = ?", one, 1, &result));
    ASSERT_INT_EQ(1, strstr(driver.error_message, "2 bind placeholder(s) but 1 value(s)") != NULL);

    driver.close(&driver);
    cleanup(path);
}

int main(void) {
    RUN_TEST(sql_scan_returns_rows_from_full_middle_pages);
    RUN_TEST(sql_index_build_sees_rows_on_full_middle_pages);
    RUN_TEST(sql_reads_and_appends_to_legacy_last_page_with_record_count);
    RUN_TEST(sql_insert_after_reopen_keeps_multi_page_chain);
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
    RUN_TEST(sql_bound_insert_values_of_each_type);
    RUN_TEST(sql_bound_where_update_delete);
    RUN_TEST(sql_bound_in_list_like_and_limit);
    RUN_TEST(sql_bound_string_is_data_not_sql);
    RUN_TEST(sql_bound_question_mark_in_literal_is_not_a_placeholder);
    RUN_TEST(sql_bound_placeholder_count_must_match);
    RUN_TEST(sql_bound_insert_select_keeps_placeholder_positions);
    RUN_TEST(sql_bound_view_definition_rejects_placeholders);
    RUN_TEST(custom_driver_binds_params_for_exec_and_query);
    RUN_TEST(custom_driver_reports_placeholder_count_mismatch);
    TEST_SUMMARY();
}
