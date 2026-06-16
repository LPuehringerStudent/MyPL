#include <string.h>

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

static int is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static Token make_token(Lexer* lexer, TokenType type);
static Token error_token(Lexer* lexer, const char* message);

static char peek_next(Lexer* lexer) {
    if (is_at_end(lexer)) return '\0';
    return lexer->current[1];
}

static Token number(Lexer* lexer) {
    while (is_digit(peek(lexer))) {
        advance(lexer);
    }

    if (peek(lexer) == '.' && is_digit(peek_next(lexer))) {
        advance(lexer); /* consume '.' */
        while (is_digit(peek(lexer))) {
            advance(lexer);
        }
        return make_token(lexer, TOKEN_FLOAT);
    }

    return make_token(lexer, TOKEN_INT);
}

static Token string(Lexer* lexer) {
    while (peek(lexer) != '"' && !is_at_end(lexer)) {
        if (peek(lexer) == '\n') lexer->line++;
        advance(lexer);
    }

    if (is_at_end(lexer)) {
        return error_token(lexer, "unterminated string");
    }

    advance(lexer); /* closing quote */
    return make_token(lexer, TOKEN_STRING);
}

static TokenType identifier_type(Lexer* lexer);

static Token identifier(Lexer* lexer) {
    while (is_alpha(peek(lexer)) || is_digit(peek(lexer))) {
        advance(lexer);
    }
    return make_token(lexer, identifier_type(lexer));
}

static TokenType identifier_type(Lexer* lexer) {
    int length = (int)(lexer->current - lexer->start);
    switch (lexer->start[0]) {
        case 'f':
            if (length == 3 && memcmp(lexer->start, "for", 3) == 0) return TOKEN_FOR;
            if (length == 5 && memcmp(lexer->start, "float", 5) == 0) return TOKEN_FLOAT;
            break;
        case 'i':
            if (length == 2 && memcmp(lexer->start, "if", 2) == 0) return TOKEN_IF;
            if (length == 3 && memcmp(lexer->start, "int", 3) == 0) return TOKEN_INT;
            if (length == 2 && memcmp(lexer->start, "in", 2) == 0) return TOKEN_IN;
            break;
        case 'p':
            if (length == 4 && memcmp(lexer->start, "proc", 4) == 0) return TOKEN_PROC;
            break;
        case 'r':
            if (length == 6 && memcmp(lexer->start, "return", 6) == 0) return TOKEN_RETURN;
            break;
    }
    return TOKEN_IDENT;
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

static void skip_whitespace(Lexer* lexer) {
    for (;;) {
        char c = peek(lexer);
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance(lexer);
                break;
            case '\n':
                lexer->line++;
                advance(lexer);
                break;
            case '/':
                if (peek_next(lexer) == '/') {
                    while (peek(lexer) != '\n' && !is_at_end(lexer)) {
                        advance(lexer);
                    }
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

Token lexer_next_token(Lexer* lexer) {
    skip_whitespace(lexer);
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
        case '_':
        case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
        case 'g': case 'h': case 'i': case 'j': case 'k': case 'l':
        case 'm': case 'n': case 'o': case 'p': case 'q': case 'r':
        case 's': case 't': case 'u': case 'v': case 'w': case 'x':
        case 'y': case 'z':
        case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
        case 'G': case 'H': case 'I': case 'J': case 'K': case 'L':
        case 'M': case 'N': case 'O': case 'P': case 'Q': case 'R':
        case 'S': case 'T': case 'U': case 'V': case 'W': case 'X':
        case 'Y': case 'Z':
            return identifier(lexer);
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            return number(lexer);
        case '"': return string(lexer);
    }

    return error_token(lexer, "unexpected character");
}
