#include "test_harness.h"
#include "compiler.h"
#include "vm.h"
#include "sql_engine.h"

TEST(compiler_compiles_integer_return) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { return 42; }", &chunk));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(42, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_local_variables) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { int x = 10; x = 20; return x; }", &chunk));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(20, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_arithmetic) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { return 1 + 2 * 3; }", &chunk));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(7, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_comparison) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { return 5 == 5; }", &chunk));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(1, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_if_statement) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { if 0 { return 13; } return 42; }", &chunk));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(42, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_if_true_branch) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { if 1 { return 13; } return 42; }", &chunk));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(13, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_else_branch) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { if 0 { return 13; } else { return 42; } }", &chunk));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(42, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_unary_minus) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { return -7; }", &chunk));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(-7, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_unary_not) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { return !0; }", &chunk));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(1, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_unary_not_true) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { return !1; }", &chunk));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(0, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_float_return) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> float { return 3.14; }", &chunk));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_FLOAT_EQ(3.14, vm_pop(vm).as.as_float);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_float_arithmetic) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> float { return 1.5 + 2.5; }", &chunk));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_FLOAT_EQ(4.0, vm_pop(vm).as.as_float);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_mixed_int_float_arithmetic) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> float { return 1 + 2.5; }", &chunk));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_FLOAT_EQ(3.5, vm_pop(vm).as.as_float);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_string_return) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> string { return \"hello\"; }", &chunk));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    Value result = vm_pop(vm);
    ASSERT_INT_EQ(VAL_STRING, result.type);
    ASSERT_STRING_EQ("hello", result.as.as_string);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_procedure_call) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc seven() -> int { return 7; } proc main() -> int { return seven(); }", &chunk));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(7, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_forward_procedure_call) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { return later(); } proc later() -> int { return 9; }", &chunk));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(9, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_procedure_with_parameters) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc add(a int, b int) -> int { return a + b; } proc main() -> int { return add(3, 4); }", &chunk));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(7, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_procedure_with_parameter_and_local) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc f(x int) -> int { int y = 10; return x + y; } proc main() -> int { return f(5); }", &chunk));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(15, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_forward_call_with_arguments) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { return later(2, 3); } proc later(a int, b int) -> int { return a * b; }", &chunk));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(6, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_for_sql_loop) {
    catalog_clear();
    const char* cols[] = {"id"};
    int types[] = {VAL_INT};
    Table* t = catalog_create_table("users", cols, types, 1);

    Cell cells[1];
    cells[0].type = VAL_INT;
    cells[0].as.as_int = 1;
    catalog_insert(t, cells);

    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { for row in SELECT id FROM users { return row.id; } return 0; }", &chunk));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(1, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
    catalog_clear();
}

TEST(compiler_compiles_for_sql_loop_sum) {
    catalog_clear();
    const char* cols[] = {"id"};
    int types[] = {VAL_INT};
    Table* t = catalog_create_table("users", cols, types, 1);

    Cell cells[1];
    for (int i = 1; i <= 3; i++) {
        cells[0].type = VAL_INT;
        cells[0].as.as_int = i;
        catalog_insert(t, cells);
    }

    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { int sum = 0; for row in SELECT id FROM users { sum = sum + row.id; } return sum; }", &chunk));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(6, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
    catalog_clear();
}

TEST(compiler_block_scope_does_not_leak_locals) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { if 1 { int x = 10; } int y = 5; return y; }", &chunk));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(5, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_reports_undefined_variable) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(0, compile("proc main() -> int { return unknown; }", &chunk));
    free_chunk(&chunk);
}

int main(void) {
    RUN_TEST(compiler_compiles_integer_return);
    RUN_TEST(compiler_compiles_local_variables);
    RUN_TEST(compiler_compiles_arithmetic);
    RUN_TEST(compiler_compiles_comparison);
    RUN_TEST(compiler_compiles_if_statement);
    RUN_TEST(compiler_compiles_if_true_branch);
    RUN_TEST(compiler_compiles_else_branch);
    RUN_TEST(compiler_compiles_unary_minus);
    RUN_TEST(compiler_compiles_unary_not);
    RUN_TEST(compiler_compiles_unary_not_true);
    RUN_TEST(compiler_compiles_float_return);
    RUN_TEST(compiler_compiles_float_arithmetic);
    RUN_TEST(compiler_compiles_mixed_int_float_arithmetic);
    RUN_TEST(compiler_compiles_string_return);
    RUN_TEST(compiler_compiles_procedure_call);
    RUN_TEST(compiler_compiles_forward_procedure_call);
    RUN_TEST(compiler_compiles_procedure_with_parameters);
    RUN_TEST(compiler_compiles_procedure_with_parameter_and_local);
    RUN_TEST(compiler_compiles_forward_call_with_arguments);
    RUN_TEST(compiler_compiles_for_sql_loop);
    RUN_TEST(compiler_compiles_for_sql_loop_sum);
    RUN_TEST(compiler_block_scope_does_not_leak_locals);
    RUN_TEST(compiler_reports_undefined_variable);
    TEST_SUMMARY();
}
