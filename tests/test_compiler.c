#include "test_harness.h"
#include "compiler.h"
#include "vm.h"

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

int main(void) {
    RUN_TEST(compiler_compiles_integer_return);
    RUN_TEST(compiler_compiles_local_variables);
    RUN_TEST(compiler_compiles_arithmetic);
    RUN_TEST(compiler_compiles_comparison);
    RUN_TEST(compiler_compiles_if_statement);
    RUN_TEST(compiler_compiles_if_true_branch);
    RUN_TEST(compiler_compiles_procedure_call);
    RUN_TEST(compiler_compiles_forward_procedure_call);
    TEST_SUMMARY();
}
