#include "lexer.h"

void lexer_init(Lexer* lexer, const char* source) {
    lexer->start = source;
    lexer->current = source;
    lexer->line = 1;
}

static int is_at_end(Lexer* lexer) {
    return *lexer->current == '\0';
}

static char advance(Lexer* lexer) {
    lexer->current++;
    return lexer->current[-1];
}

static char peek(Lexer* lexer) {
    return *lexer->current;
}

static int match(Lexer* lexer, char expected) {
    if (is_at_end(lexer)) return 0;
    if (*lexer->current != expected) return 0;
    lexer->current++;
    return 1;
}

static Token make_token(Lexer* lexer, TokenType type) {
    Token token;
    token.type = type;
    token.start = lexer->start;
    token.length = (int)(lexer->current - lexer->start);
    token.line = lexer->line;
    return token;
}

static Token error_token(Lexer* lexer, const char* message) {
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = 0;
    token.line = lexer->line;
    return token;
}

Token lexer_next_token(Lexer* lexer) {
    lexer->start = lexer->current;

    if (is_at_end(lexer)) {
        return make_token(lexer, TOKEN_EOF);
    }

    char c = advance(lexer);

    switch (c) {
        case '(': return make_token(lexer, TOKEN_LPAREN);
        case ')': return make_token(lexer, TOKEN_RPAREN);
        case '{': return make_token(lexer, TOKEN_LBRACE);
        case '}': return make_token(lexer, TOKEN_RBRACE);
        case '+': return make_token(lexer, TOKEN_PLUS);
        case '-':
            if (match(lexer, '>')) return make_token(lexer, TOKEN_ARROW);
            return make_token(lexer, TOKEN_MINUS);
        case '*': return make_token(lexer, TOKEN_STAR);
        case '/': return make_token(lexer, TOKEN_SLASH);
        case ',': return make_token(lexer, TOKEN_COMMA);
        case ';': return make_token(lexer, TOKEN_SEMICOLON);
        case ':': return make_token(lexer, TOKEN_COLON);
        case '<':
            if (match(lexer, '=')) return make_token(lexer, TOKEN_LE);
            return make_token(lexer, TOKEN_LT);
        case '>':
            if (match(lexer, '=')) return make_token(lexer, TOKEN_GE);
            return make_token(lexer, TOKEN_GT);
        case '=':
            if (match(lexer, '=')) return make_token(lexer, TOKEN_EQ);
            return make_token(lexer, TOKEN_ASSIGN);
        case '!':
            if (match(lexer, '=')) return make_token(lexer, TOKEN_NE);
            return make_token(lexer, TOKEN_BANG);
    }

    return error_token(lexer, "unexpected character");
}
