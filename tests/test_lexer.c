#include "test_harness.h"
#include "lexer.h"

TEST(lexer_returns_eof_for_empty_source) {
    Lexer lexer;
    lexer_init(&lexer, "");
    Token token = lexer_next_token(&lexer);
    ASSERT_INT_EQ(TOKEN_EOF, token.type);
}

TEST(lexer_scans_single_char_tokens) {
    Lexer lexer;
    lexer_init(&lexer, "(){}+,;*-/");
    ASSERT_INT_EQ(TOKEN_LPAREN, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_RPAREN, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_LBRACE, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_RBRACE, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_PLUS, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_COMMA, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_SEMICOLON, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_STAR, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_MINUS, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_SLASH, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);

    lexer_init(&lexer, "<");
    ASSERT_INT_EQ(TOKEN_LT, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);

    lexer_init(&lexer, ">");
    ASSERT_INT_EQ(TOKEN_GT, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);

    lexer_init(&lexer, "=");
    ASSERT_INT_EQ(TOKEN_ASSIGN, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);

    lexer_init(&lexer, "!");
    ASSERT_INT_EQ(TOKEN_BANG, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);
}

TEST(lexer_scans_two_char_operators) {
    Lexer lexer;
    lexer_init(&lexer, "==!=>=<=->");
    ASSERT_INT_EQ(TOKEN_EQ, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_NE, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_GE, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_LE, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_ARROW, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);
}

TEST(lexer_scans_keywords) {
    Lexer lexer;

    lexer_init(&lexer, "proc");
    ASSERT_INT_EQ(TOKEN_PROC, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);

    lexer_init(&lexer, "for");
    ASSERT_INT_EQ(TOKEN_FOR, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);

    lexer_init(&lexer, "if");
    ASSERT_INT_EQ(TOKEN_IF, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);

    lexer_init(&lexer, "return");
    ASSERT_INT_EQ(TOKEN_RETURN, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);

    lexer_init(&lexer, "in");
    ASSERT_INT_EQ(TOKEN_IN, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);

    lexer_init(&lexer, "int");
    ASSERT_INT_EQ(TOKEN_INT, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);

    lexer_init(&lexer, "float");
    ASSERT_INT_EQ(TOKEN_FLOAT, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);
}

TEST(lexer_scans_identifiers) {
    Lexer lexer;

    lexer_init(&lexer, "foo");
    Token t1 = lexer_next_token(&lexer);
    ASSERT_INT_EQ(TOKEN_IDENT, t1.type);
    ASSERT_INT_EQ(3, t1.length);

    lexer_init(&lexer, "_bar");
    Token t2 = lexer_next_token(&lexer);
    ASSERT_INT_EQ(TOKEN_IDENT, t2.type);
    ASSERT_INT_EQ(4, t2.length);

    lexer_init(&lexer, "baz123");
    Token t3 = lexer_next_token(&lexer);
    ASSERT_INT_EQ(TOKEN_IDENT, t3.type);
    ASSERT_INT_EQ(6, t3.length);
}

int main(void) {
    RUN_TEST(lexer_returns_eof_for_empty_source);
    RUN_TEST(lexer_scans_single_char_tokens);
    RUN_TEST(lexer_scans_two_char_operators);
    RUN_TEST(lexer_scans_keywords);
    RUN_TEST(lexer_scans_identifiers);
    TEST_SUMMARY();
}
