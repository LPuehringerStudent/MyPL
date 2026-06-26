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

int main(void) {
    RUN_TEST(typecheck_rejects_string_to_int_assignment);
    RUN_TEST(typecheck_accepts_valid_program);
    RUN_TEST(typecheck_rejects_typed_array_mismatch);
    RUN_TEST(typecheck_accepts_int_float_coercion);
    TEST_SUMMARY();
}
