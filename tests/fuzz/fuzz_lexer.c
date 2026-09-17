#include "fuzz_common.h"

#include "lexer.h"

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    char* source = fuzz_cstring(data, size);
    if (source == NULL) return 0;
    size_t len = strlen(source);

    Lexer lexer;
    lexer_init(&lexer, source);
    /* Every token other than EOF consumes at least one character, so more
     * than len + 1 tokens means the lexer stopped making progress. */
    size_t token_count = 0;
    for (;;) {
        Token token = lexer_next_token(&lexer);
        token_count++;
        FUZZ_CHECK(token_count <= len + 1, "lexer made no progress");
        if (token.type == TOKEN_EOF) break;
        if (token.type == TOKEN_ERROR) continue;
        FUZZ_CHECK(token.length >= 0, "negative token length");
        FUZZ_CHECK(token.start >= source && token.start + token.length <= source + len,
                   "token outside source buffer");
        FUZZ_CHECK(token.line >= 1, "token line before 1");
    }

    free(source);
    return 0;
}
