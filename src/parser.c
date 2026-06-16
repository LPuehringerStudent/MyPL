#include <stdlib.h>
#include <string.h>

#include "parser.h"
#include "lexer.h"

typedef struct {
    Lexer lexer;
    Token current;
    Token previous;
    int had_error;
} Parser;

static void advance(Parser* parser) {
    parser->previous = parser->current;
    parser->current = lexer_next_token(&parser->lexer);
}

static int check(Parser* parser, TokenType type) {
    return parser->current.type == type;
}

static int match(Parser* parser, TokenType type) {
    if (!check(parser, type)) return 0;
    advance(parser);
    return 1;
}

static char* copy_token_lexeme(const Token* token) {
    char* lexeme = malloc((size_t)token->length + 1);
    memcpy(lexeme, token->start, (size_t)token->length);
    lexeme[token->length] = '\0';
    return lexeme;
}

static Expr* expression(Parser* parser);

static Expr* primary(Parser* parser) {
    if (match(parser, TOKEN_INT)) {
        int value = 0;
        for (int i = 0; i < parser->previous.length; i++) {
            value = value * 10 + (parser->previous.start[i] - '0');
        }
        return create_literal_expr(value_int(value));
    }

    if (match(parser, TOKEN_FLOAT)) {
        /* TODO: parse float lexeme */
        return create_literal_expr(value_float(0.0));
    }

    if (match(parser, TOKEN_STRING)) {
        /* TODO: copy unescaped string content */
        return create_literal_expr(value_string(NULL));
    }

    if (match(parser, TOKEN_IDENT)) {
        return create_variable_expr(copy_token_lexeme(&parser->previous));
    }

    if (match(parser, TOKEN_LPAREN)) {
        Expr* expr = expression(parser);
        advance(parser); /* consume ) */
        return expr;
    }

    parser->had_error = 1;
    return NULL;
}

static Expr* expression(Parser* parser) {
    return primary(parser);
}

Expr* parse_expression(const char* source) {
    Parser parser;
    lexer_init(&parser.lexer, source);
    parser.had_error = 0;
    advance(&parser);
    Expr* expr = expression(&parser);
    if (parser.had_error) {
        return NULL;
    }
    return expr;
}

Program* parse(const char* source) {
    (void)source;
    return NULL;
}
