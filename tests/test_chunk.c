#include <string.h>
#include "test_harness.h"
#include "compiler.h"

TEST(init_chunk_clears_fields) {
    Chunk chunk;
    memset(&chunk, 0xFF, sizeof(chunk));
    init_chunk(&chunk);
    ASSERT_INT_EQ(0, chunk.count);
    ASSERT_INT_EQ(0, chunk.capacity);
    ASSERT_PTR_NULL(chunk.code);
    ASSERT_PTR_NULL(chunk.constants);
}

TEST(write_chunk_stores_bytes) {
    Chunk chunk;
    init_chunk(&chunk);
    write_chunk(&chunk, OP_RETURN);
    ASSERT_INT_EQ(1, chunk.count);
    ASSERT_INT_EQ(OP_RETURN, chunk.code[0]);
    write_chunk(&chunk, OP_CONST);
    ASSERT_INT_EQ(2, chunk.count);
    ASSERT_INT_EQ(OP_CONST, chunk.code[1]);
}

TEST(add_constant_appends_values) {
    Chunk chunk;
    init_chunk(&chunk);
    int idx0 = add_constant(&chunk, value_int(42));
    ASSERT_INT_EQ(0, idx0);
    ASSERT_INT_EQ(1, chunk.constants_count);
    ASSERT_INT_EQ(42, chunk.constants[0].as.as_int);
    int idx1 = add_constant(&chunk, value_int(7));
    ASSERT_INT_EQ(1, idx1);
    ASSERT_INT_EQ(2, chunk.constants_count);
    ASSERT_INT_EQ(7, chunk.constants[1].as.as_int);
}

TEST(free_chunk_releases_memory) {
    Chunk chunk;
    init_chunk(&chunk);
    write_chunk(&chunk, OP_RETURN);
    add_constant(&chunk, value_int(1));
    free_chunk(&chunk);
    ASSERT_PTR_NULL(chunk.code);
    ASSERT_PTR_NULL(chunk.constants);
    ASSERT_INT_EQ(0, chunk.count);
    ASSERT_INT_EQ(0, chunk.capacity);
    ASSERT_INT_EQ(0, chunk.constants_count);
    ASSERT_INT_EQ(0, chunk.constants_capacity);
}

TEST(chunk_can_store_16_bit_operands) {
    Chunk chunk;
    init_chunk(&chunk);
    write_chunk_u16(&chunk, 0x1234);
    ASSERT_INT_EQ(2, chunk.count);
    ASSERT_INT_EQ(0x12, chunk.code[0]);
    ASSERT_INT_EQ(0x34, chunk.code[1]);
    uint16_t value = read_u16(chunk.code);
    ASSERT_INT_EQ(0x1234, (int)value);
    free_chunk(&chunk);
}

int main(void) {
    RUN_TEST(init_chunk_clears_fields);
    RUN_TEST(write_chunk_stores_bytes);
    RUN_TEST(add_constant_appends_values);
    RUN_TEST(free_chunk_releases_memory);
    RUN_TEST(chunk_can_store_16_bit_operands);
    TEST_SUMMARY();
}
