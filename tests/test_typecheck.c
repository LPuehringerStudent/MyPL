#include <stdlib.h>

#include "test_harness.h"
#include "parser.h"
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

TEST(typecheck_rejects_undefined_procedure_call) {
    char error[256];
    Program* program = parse("proc main() -> int { foo(); return 0; }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(0, typecheck_program(program, NULL, 0, NULL, error, sizeof(error)));
    free_program(program);
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
    TEST_SUMMARY();
}
