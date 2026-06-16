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

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,
    PREC_OR,
    PREC_AND,
    PREC_EQUALITY,
    PREC_COMPARISON,
    PREC_TERM,
    PREC_FACTOR,
    PREC_UNARY,
    PREC_CALL,
    PREC_PRIMARY
} Precedence;

typedef Expr* (*PrefixFn)(Parser* parser);
typedef Expr* (*InfixFn)(Parser* parser, Expr* left);

typedef struct {
    PrefixFn prefix;
    InfixFn infix;
    Precedence precedence;
} ParseRule;

static ParseRule* get_rule(TokenType type);
static Expr* parse_precedence(Parser* parser, Precedence precedence);

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

static Expr* grouping(Parser* parser) {
    Expr* expr = expression(parser);
    advance(parser); /* consume ) */
    return expr;
}

static Expr* number(Parser* parser) {
    if (parser->previous.type == TOKEN_FLOAT) {
        /* TODO: parse float lexeme */
        return create_literal_expr(value_float(0.0));
    }
    int value = 0;
    for (int i = 0; i < parser->previous.length; i++) {
        value = value * 10 + (parser->previous.start[i] - '0');
    }
    return create_literal_expr(value_int(value));
}

static Expr* variable(Parser* parser) {
    return create_variable_expr(copy_token_lexeme(&parser->previous));
}

static Expr* binary(Parser* parser, Expr* left) {
    TokenType op = parser->previous.type;
    ParseRule* rule = get_rule(op);
    Expr* right = parse_precedence(parser, (Precedence)(rule->precedence + 1));
    return create_binary_expr(op, left, right);
}

static Expr* parse_precedence(Parser* parser, Precedence precedence) {
    advance(parser);
    PrefixFn prefix_rule = get_rule(parser->previous.type)->prefix;
    if (prefix_rule == NULL) {
        parser->had_error = 1;
        return NULL;
    }

    Expr* left = prefix_rule(parser);

    while (precedence <= get_rule(parser->current.type)->precedence) {
        advance(parser);
        InfixFn infix_rule = get_rule(parser->previous.type)->infix;
        left = infix_rule(parser, left);
    }

    return left;
}

static Expr* expression(Parser* parser) {
    return parse_precedence(parser, PREC_ASSIGNMENT);
}

static ParseRule rules[] = {
    [TOKEN_PROC]       = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FOR]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_IF]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_RETURN]     = {NULL,     NULL,   PREC_NONE},
    [TOKEN_IN]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_INT_TYPE]   = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FLOAT_TYPE] = {NULL,     NULL,   PREC_NONE},
    [TOKEN_IDENT]      = {variable, NULL,   PREC_NONE},
    [TOKEN_INT]        = {number,   NULL,   PREC_NONE},
    [TOKEN_FLOAT]      = {number,   NULL,   PREC_NONE},
    [TOKEN_STRING]     = {number,   NULL,   PREC_NONE},
    [TOKEN_SQL_QUERY]  = {NULL,     NULL,   PREC_NONE},

    [TOKEN_ARROW]      = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EQ]         = {NULL,     binary, PREC_EQUALITY},
    [TOKEN_NE]         = {NULL,     binary, PREC_EQUALITY},
    [TOKEN_GE]         = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_LE]         = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_ASSIGN]     = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LT]         = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_GT]         = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_BANG]       = {NULL,     NULL,   PREC_NONE},

    [TOKEN_PLUS]       = {NULL,     binary, PREC_TERM},
    [TOKEN_MINUS]      = {NULL,     binary, PREC_TERM},
    [TOKEN_STAR]       = {NULL,     binary, PREC_FACTOR},
    [TOKEN_SLASH]      = {NULL,     binary, PREC_FACTOR},
    [TOKEN_DOT]        = {NULL,     NULL,   PREC_NONE},

    [TOKEN_LPAREN]     = {grouping, NULL,   PREC_NONE},
    [TOKEN_RPAREN]     = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LBRACE]     = {NULL,     NULL,   PREC_NONE},
    [TOKEN_RBRACE]     = {NULL,     NULL,   PREC_NONE},
    [TOKEN_COMMA]      = {NULL,     NULL,   PREC_NONE},
    [TOKEN_SEMICOLON]  = {NULL,     NULL,   PREC_NONE},
    [TOKEN_COLON]      = {NULL,     NULL,   PREC_NONE},

    [TOKEN_ERROR]      = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EOF]        = {NULL,     NULL,   PREC_NONE},
};

static ParseRule* get_rule(TokenType type) {
    return &rules[type];
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
