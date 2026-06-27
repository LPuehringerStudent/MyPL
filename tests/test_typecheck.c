#include <stdlib.h>
#include <unistd.h>

#include "test_harness.h"
#include "parser.h"
#include "sql_engine.h"
#include "typecheck.h"

TEST(typecheck_rejects_string_to_int_assignment) {
    char error[256];
    Program* program = parse("proc main() -> int { int x = \"hello\"; return 0; }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(0, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_accepts_valid_program) {
    char error[256];
    Program* program = parse("proc main() -> int { int x = 1 + 2; return x; }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(1, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_rejects_typed_array_mismatch) {
    char error[256];
    Program* program = parse("proc main() -> int { array<int> a = [1, \"two\"]; return 0; }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(0, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_accepts_int_float_coercion) {
    char error[256];
    Program* program = parse("proc f(x float) -> float { return x + 1; } proc main() -> int { return 0; }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(1, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_rejects_bool_mismatch) {
    char error[256];
    Program* program = parse("proc main() -> int { bool b = 1; return 0; }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(0, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_accepts_unary_operators) {
    char error[256];
    Program* program = parse("proc main() -> int { int a = -5; bool b = !true; return a; }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(1, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_rejects_bad_unary) {
    char error[256];
    Program* program = parse("proc main() -> int { int a = -\"hello\"; return 0; }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(0, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_accepts_comparisons) {
    char error[256];
    Program* program = parse("proc main() -> int { bool a = 1 < 2; bool b = \"x\" == \"y\"; return 0; }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(1, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_rejects_mixed_comparison) {
    char error[256];
    Program* program = parse("proc main() -> int { bool a = 1 < \"x\"; return 0; }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(0, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_accepts_indexing_and_index_assign) {
    char error[256];
    Program* program = parse("proc main() -> int { array<int> a = [1, 2]; int x = a[0]; a[1] = 3; return x; }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(1, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_rejects_indexing_non_int_index) {
    char error[256];
    Program* program = parse("proc main() -> int { array<int> a = [1, 2]; int x = a[\"x\"]; return 0; }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(0, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_rejects_if_string_condition) {
    char error[256];
    Program* program = parse("proc main() -> int { if \"bad\" { } return 0; }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(0, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_rejects_narrowing_float_to_int) {
    char error[256];
    Program* program = parse("proc main() -> int { int x = 1.5; return 0; }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(0, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_rejects_mixed_int_float_array) {
    char error[256];
    Program* program = parse("proc main() -> int { array<int> a = [1, 2.5]; return 0; }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(0, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_rejects_out_of_scope_block_variable) {
    char error[256];
    Program* program = parse("proc main() -> int { if true { int x = 1; } return x; }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(0, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_accepts_external_proc_signature_arguments) {
    char error[256];
    Type* params[] = { &type_int, &type_int };
    ProcSignature sig = { "add", &type_int, params, 2 };
    ProcSignature procs[] = { sig };
    Program* program = parse("proc main() -> int { int x = add(1, 2); return x; }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(1, typecheck_program(program, procs, 1, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_accepts_for_row_in_select_loop) {
    char error[256];
    Program* program = parse("proc main() -> int { for row in SELECT id FROM users { return 0; } return 0; }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(1, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_accepts_mixed_int_float_comparison) {
    char error[256];
    Program* program = parse("proc main() -> int { bool b = 1 < 2.5; return 0; }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(1, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_accepts_int_to_float_assignment) {
    char error[256];
    Program* program = parse("proc main() -> int { float x = 1; return 0; }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(1, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_accepts_int_to_float_return) {
    char error[256];
    Program* program = parse("proc f() -> float { return 1; } proc main() -> int { return 0; }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(1, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_accepts_row_field_return) {
    char error[256];
    Program* program = parse("proc main() -> int { for row in SELECT id FROM users { return row.id; } return 0; }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(1, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_rejects_undefined_variable_assignment) {
    char error[256];
    Program* program = parse("proc main() -> int { x = 1; return 0; }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(0, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_accepts_intra_program_call) {
    char error[256];
    Program* program = parse(
        "proc add(a int, b int) -> int { return a + b; } "
        "proc main() -> int { int x = add(1, 2); return x; }",
        error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(1, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_rejects_undefined_procedure_call) {
    char error[256];
    Program* program = parse("proc main() -> int { foo(); return 0; }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(0, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_accepts_uninitialized_variable) {
    char error[256];
    Program* program = parse("proc main() -> int { int x; return 0; }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(1, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_rejects_intra_program_wrong_arg_count) {
    char error[256];
    Program* program = parse(
        "proc add(a int, b int) -> int { return a + b; } "
        "proc main() -> int { int x = add(1); return x; }",
        error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(0, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_rejects_intra_program_wrong_arg_type) {
    char error[256];
    Program* program = parse(
        "proc add(a int, b int) -> int { return a + b; } "
        "proc main() -> int { int x = add(1, \"two\"); return x; }",
        error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(0, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_accepts_length_on_array) {
    char error[256];
    Program* program = parse("proc main() -> int { array<int> a = [1, 2]; return length(a); }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(1, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_rejects_length_on_int) {
    char error[256];
    Program* program = parse("proc main() -> int { return length(42); }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(0, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_accepts_append_matching_type) {
    char error[256];
    Program* program = parse("proc main() -> int { array<int> a = [1]; a = append(a, 2); return length(a); }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(1, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_rejects_append_mismatch) {
    char error[256];
    Program* program = parse("proc main() -> int { array<int> a = [1]; a = append(a, \"x\"); return 0; }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(0, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_accepts_println) {
    char error[256];
    Program* program = parse("proc main() -> int { println(42); return 0; }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(1, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_accepts_clock) {
    char error[256];
    Program* program = parse("proc main() -> int { int t = clock(); return 0; }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(1, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_rejects_clock_with_args) {
    char error[256];
    Program* program = parse("proc main() -> int { clock(1); return 0; }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(0, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_accepts_empty_array_return_with_hint) {
    char error[256];
    Program* program = parse("proc make() -> array<int> { return []; } proc main() -> int { return 0; }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(1, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
}

TEST(typecheck_resolves_sql_row_field_type) {
    char db_path[] = "/tmp/mypl_test_typecheck_sql_XXXXXX.db";
    int fd = mkstemp(db_path);
    if (fd >= 0) close(fd);
    unlink(db_path);

    Context ctx;
    ctx.db_path = db_path;
    ctx.pager = NULL;
    ASSERT_INT_EQ(1, catalog_open(&ctx));
    const char* cols[] = {"id"};
    int types[] = {VAL_INT};
    Table* t = catalog_create_table(&ctx, "users", cols, types, 1);
    ASSERT_PTR_NOT_NULL(t);

    char error[256];
    Program* program = parse(
        "proc main() -> int { for row in SELECT id FROM users { return row.id; } return 0; }",
        error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(1, typecheck_program(program, NULL, 0, &ctx, error, sizeof(error)));
    free_program(program);

    catalog_close(&ctx);
    unlink(db_path);
}

TEST(typecheck_rejects_sql_row_field_type_mismatch) {
    char db_path[] = "/tmp/mypl_test_typecheck_sql2_XXXXXX.db";
    int fd = mkstemp(db_path);
    if (fd >= 0) close(fd);
    unlink(db_path);

    Context ctx;
    ctx.db_path = db_path;
    ctx.pager = NULL;
    ASSERT_INT_EQ(1, catalog_open(&ctx));
    const char* cols[] = {"name"};
    int types[] = {VAL_STRING};
    Table* t = catalog_create_table(&ctx, "users", cols, types, 1);
    ASSERT_PTR_NOT_NULL(t);

    char error[256];
    Program* program = parse(
        "proc main() -> int { for row in SELECT name FROM users { int x = row.name; return x; } return 0; }",
        error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(0, typecheck_program(program, NULL, 0, &ctx, error, sizeof(error)));
    free_program(program);

    catalog_close(&ctx);
    unlink(db_path);
}

TEST(typecheck_rejects_row_field_not_selected) {
    char db_path[] = "/tmp/mypl_test_typecheck_sql3_XXXXXX.db";
    int fd = mkstemp(db_path);
    if (fd >= 0) close(fd);
    unlink(db_path);

    Context ctx;
    ctx.db_path = db_path;
    ctx.pager = NULL;
    ASSERT_INT_EQ(1, catalog_open(&ctx));
    const char* cols[] = {"id", "name"};
    int types[] = {VAL_INT, VAL_STRING};
    Table* t = catalog_create_table(&ctx, "users", cols, types, 2);
    ASSERT_PTR_NOT_NULL(t);

    char error[256];
    Program* program = parse(
        "proc main() -> int { for row in SELECT name FROM users { return row.id; } return 0; }",
        error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(0, typecheck_program(program, NULL, 0, &ctx, error, sizeof(error)));
    free_program(program);

    catalog_close(&ctx);
    unlink(db_path);
}

TEST(typecheck_accepts_row_field_with_select_star) {
    char db_path[] = "/tmp/mypl_test_typecheck_sql4_XXXXXX.db";
    int fd = mkstemp(db_path);
    if (fd >= 0) close(fd);
    unlink(db_path);

    Context ctx;
    ctx.db_path = db_path;
    ctx.pager = NULL;
    ASSERT_INT_EQ(1, catalog_open(&ctx));
    const char* cols[] = {"id", "name"};
    int types[] = {VAL_INT, VAL_STRING};
    Table* t = catalog_create_table(&ctx, "users", cols, types, 2);
    ASSERT_PTR_NOT_NULL(t);

    char error[256];
    Program* program = parse(
        "proc main() -> int { for row in SELECT * FROM users { return row.id; } return 0; }",
        error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(1, typecheck_program(program, NULL, 0, &ctx, error, sizeof(error)));
    free_program(program);

    catalog_close(&ctx);
    unlink(db_path);
}

int main(void) {
    RUN_TEST(typecheck_rejects_string_to_int_assignment);
    RUN_TEST(typecheck_accepts_valid_program);
    RUN_TEST(typecheck_rejects_typed_array_mismatch);
    RUN_TEST(typecheck_accepts_int_float_coercion);
    RUN_TEST(typecheck_rejects_bool_mismatch);
    RUN_TEST(typecheck_accepts_unary_operators);
    RUN_TEST(typecheck_rejects_bad_unary);
    RUN_TEST(typecheck_accepts_comparisons);
    RUN_TEST(typecheck_rejects_mixed_comparison);
    RUN_TEST(typecheck_accepts_indexing_and_index_assign);
    RUN_TEST(typecheck_rejects_indexing_non_int_index);
    RUN_TEST(typecheck_rejects_if_string_condition);
    RUN_TEST(typecheck_rejects_narrowing_float_to_int);
    RUN_TEST(typecheck_rejects_mixed_int_float_array);
    RUN_TEST(typecheck_rejects_out_of_scope_block_variable);
    RUN_TEST(typecheck_accepts_external_proc_signature_arguments);
    RUN_TEST(typecheck_accepts_for_row_in_select_loop);
    RUN_TEST(typecheck_accepts_mixed_int_float_comparison);
    RUN_TEST(typecheck_accepts_int_to_float_assignment);
    RUN_TEST(typecheck_accepts_int_to_float_return);
    RUN_TEST(typecheck_accepts_row_field_return);
    RUN_TEST(typecheck_rejects_undefined_variable_assignment);
    RUN_TEST(typecheck_rejects_undefined_procedure_call);
    RUN_TEST(typecheck_accepts_intra_program_call);
    RUN_TEST(typecheck_accepts_uninitialized_variable);
    RUN_TEST(typecheck_rejects_intra_program_wrong_arg_count);
    RUN_TEST(typecheck_rejects_intra_program_wrong_arg_type);
    RUN_TEST(typecheck_accepts_length_on_array);
    RUN_TEST(typecheck_rejects_length_on_int);
    RUN_TEST(typecheck_accepts_append_matching_type);
    RUN_TEST(typecheck_rejects_append_mismatch);
    RUN_TEST(typecheck_accepts_println);
    RUN_TEST(typecheck_accepts_clock);
    RUN_TEST(typecheck_rejects_clock_with_args);
    RUN_TEST(typecheck_accepts_empty_array_return_with_hint);
    RUN_TEST(typecheck_resolves_sql_row_field_type);
    RUN_TEST(typecheck_rejects_sql_row_field_type_mismatch);
    RUN_TEST(typecheck_rejects_row_field_not_selected);
    RUN_TEST(typecheck_accepts_row_field_with_select_star);
    TEST_SUMMARY();
}
