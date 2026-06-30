#include <stdlib.h>

#include "compiler.h"

static int grow_capacity(int capacity) {
    return capacity < 8 ? 8 : capacity * 2;
}

void init_chunk(Chunk* chunk) {
    chunk->code = NULL;
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->constants = NULL;
    chunk->constants_count = 0;
    chunk->constants_capacity = 0;
}

void free_chunk(Chunk* chunk) {
    if (chunk->constants != NULL) {
        for (int i = 0; i < chunk->constants_count; i++) {
            value_release(chunk->constants[i]);
        }
    }
    free(chunk->code);
    free(chunk->constants);
    init_chunk(chunk);
}

void write_chunk(Chunk* chunk, uint8_t byte) {
    if (chunk->count >= chunk->capacity) {
        chunk->capacity = grow_capacity(chunk->capacity);
        uint8_t* new_code = realloc(chunk->code, (size_t)chunk->capacity);
        if (new_code == NULL) return;
        chunk->code = new_code;
    }
    chunk->code[chunk->count] = byte;
    chunk->count++;
}

int add_constant(Chunk* chunk, Value value) {
    if (chunk->constants_count >= chunk->constants_capacity) {
        chunk->constants_capacity = grow_capacity(chunk->constants_capacity);
        Value* new_constants = realloc(chunk->constants,
                                       sizeof(Value) * (size_t)chunk->constants_capacity);
        if (new_constants == NULL) return -1;
        chunk->constants = new_constants;
    }
    value_retain(value);
    chunk->constants[chunk->constants_count] = value;
    chunk->constants_count++;
    return chunk->constants_count - 1;
}

void write_chunk_u16(Chunk* chunk, uint16_t value) {
    write_chunk(chunk, (uint8_t)((value >> 8) & 0xFF));
    write_chunk(chunk, (uint8_t)(value & 0xFF));
}

uint16_t read_u16(const uint8_t* bytes) {
    return (uint16_t)((bytes[0] << 8) | bytes[1]);
}
