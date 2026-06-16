#include "test_harness.h"
#include "vm.h"

TEST(vm_can_push_and_pop_values) {
    VM* vm = vm_init();
    ASSERT_PTR_NOT_NULL(vm);
    vm_free(vm);
}

TEST(vm_executes_constant_and_return) {
    Chunk chunk;
    init_chunk(&chunk);
    int idx = add_constant(&chunk, value_int(42));
    write_chunk(&chunk, OP_CONST);
    write_chunk_u16(&chunk, (uint16_t)idx);
    write_chunk(&chunk, OP_RETURN);

    VM* vm = vm_init();
    InterpretResult result = vm_interpret(vm, &chunk);
    ASSERT_INT_EQ(INTERPRET_OK, result);
    Value value = vm_pop(vm);
    ASSERT_INT_EQ(42, value.as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(vm_executes_local_variables) {
    Chunk chunk;
    init_chunk(&chunk);

    int cst = add_constant(&chunk, value_int(99));
    write_chunk(&chunk, OP_CONST);
    write_chunk_u16(&chunk, (uint16_t)cst);
    write_chunk(&chunk, OP_SET_LOCAL);
    write_chunk(&chunk, 0);
    write_chunk(&chunk, OP_GET_LOCAL);
    write_chunk(&chunk, 0);
    write_chunk(&chunk, OP_RETURN);

    VM* vm = vm_init();
    InterpretResult result = vm_interpret(vm, &chunk);
    ASSERT_INT_EQ(INTERPRET_OK, result);
    ASSERT_INT_EQ(99, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(vm_executes_arithmetic) {
    Chunk chunk;
    init_chunk(&chunk);

    int a = add_constant(&chunk, value_int(10));
    int b = add_constant(&chunk, value_int(3));

    write_chunk(&chunk, OP_CONST);
    write_chunk_u16(&chunk, (uint16_t)a);
    write_chunk(&chunk, OP_CONST);
    write_chunk_u16(&chunk, (uint16_t)b);
    write_chunk(&chunk, OP_ADD);
    write_chunk(&chunk, OP_RETURN);

    VM* vm = vm_init();
    InterpretResult result = vm_interpret(vm, &chunk);
    ASSERT_INT_EQ(INTERPRET_OK, result);
    ASSERT_INT_EQ(13, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(vm_executes_subtraction) {
    Chunk chunk;
    init_chunk(&chunk);

    int a = add_constant(&chunk, value_int(10));
    int b = add_constant(&chunk, value_int(3));

    write_chunk(&chunk, OP_CONST);
    write_chunk_u16(&chunk, (uint16_t)a);
    write_chunk(&chunk, OP_CONST);
    write_chunk_u16(&chunk, (uint16_t)b);
    write_chunk(&chunk, OP_SUB);
    write_chunk(&chunk, OP_RETURN);

    VM* vm = vm_init();
    InterpretResult result = vm_interpret(vm, &chunk);
    ASSERT_INT_EQ(INTERPRET_OK, result);
    ASSERT_INT_EQ(7, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(vm_executes_multiplication) {
    Chunk chunk;
    init_chunk(&chunk);

    int a = add_constant(&chunk, value_int(6));
    int b = add_constant(&chunk, value_int(7));

    write_chunk(&chunk, OP_CONST);
    write_chunk_u16(&chunk, (uint16_t)a);
    write_chunk(&chunk, OP_CONST);
    write_chunk_u16(&chunk, (uint16_t)b);
    write_chunk(&chunk, OP_MUL);
    write_chunk(&chunk, OP_RETURN);

    VM* vm = vm_init();
    InterpretResult result = vm_interpret(vm, &chunk);
    ASSERT_INT_EQ(INTERPRET_OK, result);
    ASSERT_INT_EQ(42, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(vm_executes_division) {
    Chunk chunk;
    init_chunk(&chunk);

    int a = add_constant(&chunk, value_int(15));
    int b = add_constant(&chunk, value_int(3));

    write_chunk(&chunk, OP_CONST);
    write_chunk_u16(&chunk, (uint16_t)a);
    write_chunk(&chunk, OP_CONST);
    write_chunk_u16(&chunk, (uint16_t)b);
    write_chunk(&chunk, OP_DIV);
    write_chunk(&chunk, OP_RETURN);

    VM* vm = vm_init();
    InterpretResult result = vm_interpret(vm, &chunk);
    ASSERT_INT_EQ(INTERPRET_OK, result);
    ASSERT_INT_EQ(5, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(vm_executes_comparisons) {
    Chunk chunk;
    init_chunk(&chunk);

    int a = add_constant(&chunk, value_int(5));
    int b = add_constant(&chunk, value_int(5));

    write_chunk(&chunk, OP_CONST);
    write_chunk_u16(&chunk, (uint16_t)a);
    write_chunk(&chunk, OP_CONST);
    write_chunk_u16(&chunk, (uint16_t)b);
    write_chunk(&chunk, OP_EQ);
    write_chunk(&chunk, OP_RETURN);

    VM* vm = vm_init();
    InterpretResult result = vm_interpret(vm, &chunk);
    ASSERT_INT_EQ(INTERPRET_OK, result);
    ASSERT_INT_EQ(1, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(vm_executes_less_than) {
    Chunk chunk;
    init_chunk(&chunk);

    int a = add_constant(&chunk, value_int(3));
    int b = add_constant(&chunk, value_int(5));

    write_chunk(&chunk, OP_CONST);
    write_chunk_u16(&chunk, (uint16_t)a);
    write_chunk(&chunk, OP_CONST);
    write_chunk_u16(&chunk, (uint16_t)b);
    write_chunk(&chunk, OP_LT);
    write_chunk(&chunk, OP_RETURN);

    VM* vm = vm_init();
    InterpretResult result = vm_interpret(vm, &chunk);
    ASSERT_INT_EQ(INTERPRET_OK, result);
    ASSERT_INT_EQ(1, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(vm_executes_greater_than) {
    Chunk chunk;
    init_chunk(&chunk);

    int a = add_constant(&chunk, value_int(7));
    int b = add_constant(&chunk, value_int(2));

    write_chunk(&chunk, OP_CONST);
    write_chunk_u16(&chunk, (uint16_t)a);
    write_chunk(&chunk, OP_CONST);
    write_chunk_u16(&chunk, (uint16_t)b);
    write_chunk(&chunk, OP_GT);
    write_chunk(&chunk, OP_RETURN);

    VM* vm = vm_init();
    InterpretResult result = vm_interpret(vm, &chunk);
    ASSERT_INT_EQ(INTERPRET_OK, result);
    ASSERT_INT_EQ(1, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(vm_executes_false_comparison) {
    Chunk chunk;
    init_chunk(&chunk);

    int a = add_constant(&chunk, value_int(5));
    int b = add_constant(&chunk, value_int(3));

    write_chunk(&chunk, OP_CONST);
    write_chunk_u16(&chunk, (uint16_t)a);
    write_chunk(&chunk, OP_CONST);
    write_chunk_u16(&chunk, (uint16_t)b);
    write_chunk(&chunk, OP_LT);
    write_chunk(&chunk, OP_RETURN);

    VM* vm = vm_init();
    InterpretResult result = vm_interpret(vm, &chunk);
    ASSERT_INT_EQ(INTERPRET_OK, result);
    ASSERT_INT_EQ(0, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

int main(void) {
    RUN_TEST(vm_can_push_and_pop_values);
    RUN_TEST(vm_executes_constant_and_return);
    RUN_TEST(vm_executes_local_variables);
    RUN_TEST(vm_executes_arithmetic);
    RUN_TEST(vm_executes_subtraction);
    RUN_TEST(vm_executes_multiplication);
    RUN_TEST(vm_executes_division);
    RUN_TEST(vm_executes_comparisons);
    RUN_TEST(vm_executes_less_than);
    RUN_TEST(vm_executes_greater_than);
    RUN_TEST(vm_executes_false_comparison);
    TEST_SUMMARY();
}
