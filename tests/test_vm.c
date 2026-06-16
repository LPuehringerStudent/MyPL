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

int main(void) {
    RUN_TEST(vm_can_push_and_pop_values);
    RUN_TEST(vm_executes_constant_and_return);
    TEST_SUMMARY();
}
