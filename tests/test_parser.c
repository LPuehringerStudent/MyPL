#include <string.h>

#include "test_harness.h"
#include "parser.h"

TEST(parser_returns_empty_program_for_empty_source) {
    Program* program = parse("");
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(0, program->proc_count);
    free_program(program);
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

TEST(parser_parses_addition) {
    Expr* expr = parse_expression("1 + 2");
    ASSERT_PTR_NOT_NULL(expr);
    ASSERT_INT_EQ(EXPR_BINARY, expr->kind);
    ASSERT_INT_EQ(TOKEN_PLUS, expr->as.binary.op);
    free(expr);
}

TEST(parser_respects_precedence) {
    Expr* expr = parse_expression("1 + 2 * 3");
    ASSERT_PTR_NOT_NULL(expr);
    ASSERT_INT_EQ(EXPR_BINARY, expr->kind);
    ASSERT_INT_EQ(TOKEN_PLUS, expr->as.binary.op);
    ASSERT_INT_EQ(EXPR_BINARY, expr->as.binary.right->kind);
    ASSERT_INT_EQ(TOKEN_STAR, expr->as.binary.right->as.binary.op);
    free(expr);
}

TEST(parser_parses_comparison) {
    Expr* expr = parse_expression("x == 1");
    ASSERT_PTR_NOT_NULL(expr);
    ASSERT_INT_EQ(EXPR_BINARY, expr->kind);
    ASSERT_INT_EQ(TOKEN_EQ, expr->as.binary.op);
    free(expr);
}

TEST(parser_parses_procedure_and_var_decl) {
    Program* program = parse("proc main() -> int { int x = 42; }");
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(1, program->proc_count);
    ProcDecl* proc = &program->procs[0];
    ASSERT_INT_EQ(0, strcmp("main", proc->name));
    ASSERT_INT_EQ(TYPE_INT, proc->return_type);
    ASSERT_INT_EQ(1, proc->body->stmt_count);
    Stmt* stmt = proc->body->stmts[0];
    ASSERT_INT_EQ(STMT_VAR_DECL, stmt->kind);
    ASSERT_INT_EQ(TYPE_INT, stmt->as.var_decl.type);
    ASSERT_INT_EQ(0, strcmp("x", stmt->as.var_decl.name));
    ASSERT_INT_EQ(EXPR_LITERAL, stmt->as.var_decl.initializer->kind);
    ASSERT_INT_EQ(42, stmt->as.var_decl.initializer->as.literal.value.as.as_int);
    free_program(program);
}

int main(void) {
    RUN_TEST(parser_returns_empty_program_for_empty_source);
    RUN_TEST(parser_parses_integer_literal);
    RUN_TEST(parser_parses_identifier);
    RUN_TEST(parser_parses_addition);
    RUN_TEST(parser_respects_precedence);
    RUN_TEST(parser_parses_comparison);
    RUN_TEST(parser_parses_procedure_and_var_decl);
    TEST_SUMMARY();
}
