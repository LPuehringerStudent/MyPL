#include "compiler.h"

typedef struct {
    const char* start;
    const char* current;
} Lexer;

void lexer_init(Lexer* lexer, const char* source) {
    (void)lexer;
    (void)source;
}
