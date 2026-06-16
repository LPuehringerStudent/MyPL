#include "compiler.h"

void init_chunk(Chunk* chunk) {
    (void)chunk;
}

void free_chunk(Chunk* chunk) {
    (void)chunk;
}

void write_chunk(Chunk* chunk, uint8_t byte) {
    (void)chunk;
    (void)byte;
}

int add_constant(Chunk* chunk, Value value) {
    (void)chunk;
    (void)value;
    return 0;
}
