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

TEST(parser_parses_field_expression) {
    Expr* expr = parse_expression("row.id");
    ASSERT_PTR_NOT_NULL(expr);
    ASSERT_INT_EQ(EXPR_FIELD, expr->kind);
    ASSERT_INT_EQ(0, strcmp("row", expr->as.field.row));
    ASSERT_INT_EQ(0, strcmp("id", expr->as.field.field));
    free_expr(expr);
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

TEST(parser_parses_assignment) {
    Program* program = parse("proc main() -> int { x = 1; }");
    ASSERT_PTR_NOT_NULL(program);
    Stmt* stmt = program->procs[0].body->stmts[0];
    ASSERT_INT_EQ(STMT_ASSIGN, stmt->kind);
    ASSERT_INT_EQ(0, strcmp("x", stmt->as.assign.name));
    free_program(program);
}

TEST(parser_parses_if_statement) {
    Program* program = parse("proc main() -> int { if x == 1 { return 0; } }");
    ASSERT_PTR_NOT_NULL(program);
    Stmt* stmt = program->procs[0].body->stmts[0];
    ASSERT_INT_EQ(STMT_IF, stmt->kind);
    free_program(program);
}

TEST(parser_parses_return_statement) {
    Program* program = parse("proc main() -> int { return 42; }");
    ASSERT_PTR_NOT_NULL(program);
    Stmt* stmt = program->procs[0].body->stmts[0];
    ASSERT_INT_EQ(STMT_RETURN, stmt->kind);
    ASSERT_INT_EQ(EXPR_LITERAL, stmt->as.return_stmt.value->kind);
    free_program(program);
}

TEST(parser_parses_for_sql_loop) {
    Program* program = parse("proc main() -> int { for row in SELECT * FROM users { return 0; } }");
    ASSERT_PTR_NOT_NULL(program);
    Stmt* stmt = program->procs[0].body->stmts[0];
    ASSERT_INT_EQ(STMT_FOR, stmt->kind);
    ASSERT_INT_EQ(0, strcmp("row", stmt->as.for_stmt.var_name));
    ASSERT_INT_EQ(0, strcmp("SELECT * FROM users", stmt->as.for_stmt.sql_query));
    free_program(program);
}

int main(void) {
    RUN_TEST(parser_returns_empty_program_for_empty_source);
    RUN_TEST(parser_parses_integer_literal);
    RUN_TEST(parser_parses_identifier);
    RUN_TEST(parser_parses_addition);
    RUN_TEST(parser_respects_precedence);
    RUN_TEST(parser_parses_comparison);
    RUN_TEST(parser_parses_field_expression);
    RUN_TEST(parser_parses_procedure_and_var_decl);
    RUN_TEST(parser_parses_assignment);
    RUN_TEST(parser_parses_if_statement);
    RUN_TEST(parser_parses_return_statement);
    RUN_TEST(parser_parses_for_sql_loop);
    TEST_SUMMARY();
}
