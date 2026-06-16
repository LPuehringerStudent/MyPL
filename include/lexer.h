#ifndef MYDB_LEXER_H
#define MYDB_LEXER_H

typedef enum {
    /* keywords */
    TOKEN_PROC,
    TOKEN_FOR,
    TOKEN_IF,
    TOKEN_RETURN,
    TOKEN_IN,

    /* types */
    TOKEN_INT_TYPE,
    TOKEN_FLOAT_TYPE,

    /* literals */
    TOKEN_IDENT,
    TOKEN_INT,
    TOKEN_FLOAT,
    TOKEN_STRING,
    TOKEN_SQL_QUERY,

    /* multi-char operators */
    TOKEN_ARROW,
    TOKEN_EQ,
    TOKEN_NE,
    TOKEN_GE,
    TOKEN_LE,

    /* single-char operators */
    TOKEN_ASSIGN,
    TOKEN_LT,
    TOKEN_GT,
    TOKEN_BANG,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_DOT,

    /* delimiters */
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_COMMA,
    TOKEN_SEMICOLON,
    TOKEN_COLON,

    TOKEN_ERROR,
    TOKEN_EOF
} TokenType;

typedef struct {
    TokenType type;
    const char* start;
    int length;
    int line;
} Token;

typedef struct {
    const char* start;
    const char* current;
    int line;
} Lexer;

void lexer_init(Lexer* lexer, const char* source);
Token lexer_next_token(Lexer* lexer);

#endif
