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
    lexer_init(&lexer, "(){}+,;*-/<>=!");
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
    ASSERT_INT_EQ(TOKEN_LT, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_GT, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_ASSIGN, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_BANG, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);
}

int main(void) {
    RUN_TEST(lexer_returns_eof_for_empty_source);
    RUN_TEST(lexer_scans_single_char_tokens);
    TEST_SUMMARY();
}
