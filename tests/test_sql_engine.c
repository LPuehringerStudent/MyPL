#include "test_harness.h"
#include "sql_engine.h"
#include "compiler.h"
#include "vm.h"

TEST(sql_catalog_create_and_find_table) {
    catalog_clear();
    const char* cols[] = {"id", "name"};
    int types[] = {VAL_INT, VAL_STRING};
    Table* t = catalog_create_table("users", cols, types, 2);
    ASSERT_PTR_NOT_NULL(t);
    ASSERT_PTR_NOT_NULL(catalog_find_table("users"));
    ASSERT_PTR_NULL(catalog_find_table("missing"));
    catalog_clear();
}

TEST(sql_insert_and_select_star) {
    catalog_clear();
    const char* cols[] = {"id", "name"};
    int types[] = {VAL_INT, VAL_STRING};
    Table* t = catalog_create_table("users", cols, types, 2);

    Cell cells[2];
    cells[0].type = VAL_INT;
    cells[0].as.as_int = 1;
    cells[1].type = VAL_STRING;
    cells[1].as.as_string = "alice";
    catalog_insert(t, cells);

    cells[0].as.as_int = 2;
    cells[1].as.as_string = "bob";
    catalog_insert(t, cells);

    Context ctx = {NULL};
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

    row = result_next(res);
    ASSERT_PTR_NULL(row);

    result_free(res);
    catalog_clear();
}

TEST(sql_select_specific_columns) {
    catalog_clear();
    const char* cols[] = {"id", "name", "age"};
    int types[] = {VAL_INT, VAL_STRING, VAL_INT};
    Table* t = catalog_create_table("people", cols, types, 3);

    Cell cells[3];
    cells[0].type = VAL_INT; cells[0].as.as_int = 1;
    cells[1].type = VAL_STRING; cells[1].as.as_string = "alice";
    cells[2].type = VAL_INT; cells[2].as.as_int = 30;
    catalog_insert(t, cells);

    Context ctx = {NULL};
    Result* res = sql_exec("SELECT name, age FROM people", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(1, res->row_count);

    Row* row = result_next(res);
    ASSERT_PTR_NOT_NULL(row);
    ASSERT_INT_EQ(2, row->field_count);
    ASSERT_STRING_EQ("alice", row_get_field(row, "name").as.as_string);
    ASSERT_INT_EQ(30, row_get_field(row, "age").as.as_int);

    result_free(res);
    catalog_clear();
}

TEST(sql_where_equals_int) {
    catalog_clear();
    const char* cols[] = {"id", "name"};
    int types[] = {VAL_INT, VAL_STRING};
    Table* t = catalog_create_table("users", cols, types, 2);

    Cell cells[2];
    cells[0].type = VAL_INT; cells[0].as.as_int = 1;
    cells[1].type = VAL_STRING; cells[1].as.as_string = "alice";
    catalog_insert(t, cells);

    cells[0].as.as_int = 2;
    cells[1].as.as_string = "bob";
    catalog_insert(t, cells);

    Context ctx = {NULL};
    Result* res = sql_exec("SELECT name FROM users WHERE id = 2", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(1, res->row_count);

    Row* row = result_next(res);
    ASSERT_PTR_NOT_NULL(row);
    ASSERT_STRING_EQ("bob", row_get_field(row, "name").as.as_string);

    result_free(res);
    catalog_clear();
}

TEST(sql_where_greater_than_int) {
    catalog_clear();
    const char* cols[] = {"id", "age"};
    int types[] = {VAL_INT, VAL_INT};
    Table* t = catalog_create_table("people", cols, types, 2);

    Cell cells[2];
    cells[0].type = VAL_INT; cells[0].as.as_int = 1;
    cells[1].type = VAL_INT; cells[1].as.as_int = 20;
    catalog_insert(t, cells);

    cells[0].as.as_int = 2;
    cells[1].as.as_int = 35;
    catalog_insert(t, cells);

    cells[0].as.as_int = 3;
    cells[1].as.as_int = 40;
    catalog_insert(t, cells);

    Context ctx = {NULL};
    Result* res = sql_exec("SELECT id FROM people WHERE age > 25", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(2, res->row_count);

    Row* row = result_next(res);
    ASSERT_INT_EQ(2, row_get_field(row, "id").as.as_int);
    row = result_next(res);
    ASSERT_INT_EQ(3, row_get_field(row, "id").as.as_int);

    result_free(res);
    catalog_clear();
}

TEST(sql_select_unknown_table_returns_empty_result) {
    catalog_clear();
    Context ctx = {NULL};
    Result* res = sql_exec("SELECT id FROM missing", &ctx);
    ASSERT_PTR_NOT_NULL(res);
    ASSERT_INT_EQ(0, res->row_count);
    result_free(res);
    catalog_clear();
}

TEST(sql_compiler_loop_uses_real_table) {
    catalog_clear();
    const char* cols[] = {"id"};
    int types[] = {VAL_INT};
    Table* t = catalog_create_table("users", cols, types, 1);

    Cell cells[1];
    cells[0].type = VAL_INT; cells[0].as.as_int = 7;
    catalog_insert(t, cells);

    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { for row in SELECT id FROM users { return row.id; } return 0; }", &chunk));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(7, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
    catalog_clear();
}

int main(void) {
    RUN_TEST(sql_catalog_create_and_find_table);
    RUN_TEST(sql_insert_and_select_star);
    RUN_TEST(sql_select_specific_columns);
    RUN_TEST(sql_where_equals_int);
    RUN_TEST(sql_where_greater_than_int);
    RUN_TEST(sql_select_unknown_table_returns_empty_result);
    RUN_TEST(sql_compiler_loop_uses_real_table);
    TEST_SUMMARY();
}
