#include <string.h>

#include "test_harness.h"
#include "parser.h"

TEST(parser_returns_null_for_empty_source) {
    Program* program = parse("");
    ASSERT_PTR_NULL(program);
}

TEST(parser_parses_integer_literal) {
    Expr* expr = parse_expression("42");
    ASSERT_PTR_NOT_NULL(expr);
    ASSERT_INT_EQ(EXPR_LITERAL, expr->kind);
    ASSERT_INT_EQ(42, expr->as.literal.value.as.as_int);
    free(expr);
}

TEST(parser_parses_identifier) {
    Expr* expr = parse_expression("foo");
    ASSERT_PTR_NOT_NULL(expr);
    ASSERT_INT_EQ(EXPR_VARIABLE, expr->kind);
    ASSERT_INT_EQ(0, strcmp("foo", expr->as.variable.name));
    free(expr);
}

int main(void) {
    RUN_TEST(parser_returns_null_for_empty_source);
    RUN_TEST(parser_parses_integer_literal);
    RUN_TEST(parser_parses_identifier);
    TEST_SUMMARY();
}
