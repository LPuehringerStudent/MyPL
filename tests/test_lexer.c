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

    lexer_init(&lexer, "else");
    ASSERT_INT_EQ(TOKEN_ELSE, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);

    lexer_init(&lexer, "return");
    ASSERT_INT_EQ(TOKEN_RETURN, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);

    lexer_init(&lexer, "while");
    ASSERT_INT_EQ(TOKEN_WHILE, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);

    lexer_init(&lexer, "break");
    ASSERT_INT_EQ(TOKEN_BREAK, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);

    lexer_init(&lexer, "continue");
    ASSERT_INT_EQ(TOKEN_CONTINUE, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);

    lexer_init(&lexer, "do");
    ASSERT_INT_EQ(TOKEN_DO, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);

    lexer_init(&lexer, "in");
    ASSERT_INT_EQ(TOKEN_IN, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);

    lexer_init(&lexer, "int");
    ASSERT_INT_EQ(TOKEN_INT_TYPE, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);

    lexer_init(&lexer, "float");
    ASSERT_INT_EQ(TOKEN_FLOAT_TYPE, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);

    lexer_init(&lexer, "string");
    ASSERT_INT_EQ(TOKEN_STRING_TYPE, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);
}

TEST(lexer_distinguishes_type_keywords_from_literals) {
    Lexer lexer;
    lexer_init(&lexer, "int 1 float 1.0");
    ASSERT_INT_EQ(TOKEN_INT_TYPE, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_INT, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_FLOAT_TYPE, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_FLOAT, lexer_next_token(&lexer).type);
}

TEST(lexer_scans_dot) {
    Lexer lexer;
    lexer_init(&lexer, ".");
    ASSERT_INT_EQ(TOKEN_DOT, lexer_next_token(&lexer).type);
}

TEST(lexer_scans_sql_query) {
    Lexer lexer;
    lexer_init(&lexer, "SELECT salary FROM employees WHERE id > min_id");
    Token t = lexer_next_token(&lexer);
    ASSERT_INT_EQ(TOKEN_SQL_QUERY, t.type);
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

TEST(lexer_scans_integers) {
    Lexer lexer;

    lexer_init(&lexer, "123");
    Token t1 = lexer_next_token(&lexer);
    ASSERT_INT_EQ(TOKEN_INT, t1.type);
    ASSERT_INT_EQ(3, t1.length);

    lexer_init(&lexer, "0");
    Token t2 = lexer_next_token(&lexer);
    ASSERT_INT_EQ(TOKEN_INT, t2.type);
    ASSERT_INT_EQ(1, t2.length);

    lexer_init(&lexer, "42");
    Token t3 = lexer_next_token(&lexer);
    ASSERT_INT_EQ(TOKEN_INT, t3.type);
    ASSERT_INT_EQ(2, t3.length);
}

TEST(lexer_scans_floats) {
    Lexer lexer;

    lexer_init(&lexer, "3.14");
    Token t1 = lexer_next_token(&lexer);
    ASSERT_INT_EQ(TOKEN_FLOAT, t1.type);
    ASSERT_INT_EQ(4, t1.length);

    lexer_init(&lexer, "0.5");
    Token t2 = lexer_next_token(&lexer);
    ASSERT_INT_EQ(TOKEN_FLOAT, t2.type);
    ASSERT_INT_EQ(3, t2.length);

    lexer_init(&lexer, "10.0");
    Token t3 = lexer_next_token(&lexer);
    ASSERT_INT_EQ(TOKEN_FLOAT, t3.type);
    ASSERT_INT_EQ(4, t3.length);
}

TEST(lexer_scans_strings) {
    Lexer lexer;

    lexer_init(&lexer, "\"hello\"");
    Token t1 = lexer_next_token(&lexer);
    ASSERT_INT_EQ(TOKEN_STRING, t1.type);
    ASSERT_INT_EQ(7, t1.length);

    lexer_init(&lexer, "\"\"");
    Token t2 = lexer_next_token(&lexer);
    ASSERT_INT_EQ(TOKEN_STRING, t2.type);
    ASSERT_INT_EQ(2, t2.length);
}

TEST(lexer_reports_unterminated_string) {
    Lexer lexer;
    lexer_init(&lexer, "\"hello");
    Token t = lexer_next_token(&lexer);
    ASSERT_INT_EQ(TOKEN_ERROR, t.type);
}

TEST(lexer_skips_whitespace) {
    Lexer lexer;
    lexer_init(&lexer, "  \t\n  proc   (  ) ");
    ASSERT_INT_EQ(TOKEN_PROC, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_LPAREN, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_RPAREN, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);
}

TEST(lexer_tracks_lines) {
    Lexer lexer;
    lexer_init(&lexer, "proc\nfoo");
    ASSERT_INT_EQ(1, lexer_next_token(&lexer).line);
    ASSERT_INT_EQ(2, lexer_next_token(&lexer).line);
}

TEST(lexer_tracks_columns) {
    Lexer lexer;
    lexer_init(&lexer, "  proc  foo");
    ASSERT_INT_EQ(3, lexer_next_token(&lexer).column);
    ASSERT_INT_EQ(9, lexer_next_token(&lexer).column);

    lexer_init(&lexer, "proc\nfoo");
    ASSERT_INT_EQ(1, lexer_next_token(&lexer).column);
    ASSERT_INT_EQ(1, lexer_next_token(&lexer).column);
}

TEST(lexer_reports_unexpected_character) {
    Lexer lexer;
    lexer_init(&lexer, "@");
    Token t = lexer_next_token(&lexer);
    ASSERT_INT_EQ(TOKEN_ERROR, t.type);
}

TEST(lexer_scans_brackets) {
    Lexer lexer;
    lexer_init(&lexer, "[]");
    ASSERT_INT_EQ(TOKEN_LBRACKET, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_RBRACKET, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);
}

TEST(lexer_scans_bool_keywords_and_literals) {
    Lexer lexer;

    lexer_init(&lexer, "bool");
    ASSERT_INT_EQ(TOKEN_BOOL_TYPE, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);

    lexer_init(&lexer, "array");
    ASSERT_INT_EQ(TOKEN_ARRAY_TYPE, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);

    lexer_init(&lexer, "true");
    ASSERT_INT_EQ(TOKEN_TRUE, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);

    lexer_init(&lexer, "false");
    ASSERT_INT_EQ(TOKEN_FALSE, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);
}

TEST(lexer_skips_comments) {
    Lexer lexer;

    lexer_init(&lexer, "// whole line comment\nint x;");
    ASSERT_INT_EQ(TOKEN_INT_TYPE, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_IDENT, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_SEMICOLON, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);

    lexer_init(&lexer, "/* block comment */ int y;");
    ASSERT_INT_EQ(TOKEN_INT_TYPE, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_IDENT, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_SEMICOLON, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);

    lexer_init(&lexer, "/* multi\nline */ int z;");
    ASSERT_INT_EQ(TOKEN_INT_TYPE, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_IDENT, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_SEMICOLON, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);

    lexer_init(&lexer, "int a; // trailing comment\nint b;");
    ASSERT_INT_EQ(TOKEN_INT_TYPE, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_IDENT, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_SEMICOLON, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_INT_TYPE, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_IDENT, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_SEMICOLON, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);
}

TEST(lexer_scans_import_keyword) {
    Lexer lexer;
    lexer_init(&lexer, "import");
    ASSERT_INT_EQ(TOKEN_IMPORT, lexer_next_token(&lexer).type);
    ASSERT_INT_EQ(TOKEN_EOF, lexer_next_token(&lexer).type);
}

int main(void) {
    RUN_TEST(lexer_returns_eof_for_empty_source);
    RUN_TEST(lexer_scans_single_char_tokens);
    RUN_TEST(lexer_scans_two_char_operators);
    RUN_TEST(lexer_scans_keywords);
    RUN_TEST(lexer_distinguishes_type_keywords_from_literals);
    RUN_TEST(lexer_scans_dot);
    RUN_TEST(lexer_scans_sql_query);
    RUN_TEST(lexer_scans_identifiers);
    RUN_TEST(lexer_scans_integers);
    RUN_TEST(lexer_scans_floats);
    RUN_TEST(lexer_scans_strings);
    RUN_TEST(lexer_reports_unterminated_string);
    RUN_TEST(lexer_skips_whitespace);
    RUN_TEST(lexer_tracks_lines);
    RUN_TEST(lexer_tracks_columns);
    RUN_TEST(lexer_reports_unexpected_character);
    RUN_TEST(lexer_scans_brackets);
    RUN_TEST(lexer_scans_bool_keywords_and_literals);
    RUN_TEST(lexer_skips_comments);
    RUN_TEST(lexer_scans_import_keyword);
    TEST_SUMMARY();
}
