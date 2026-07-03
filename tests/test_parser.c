#include <string.h>

#include "test_harness.h"
#include "parser.h"

TEST(parser_returns_empty_program_for_empty_source) {
    Program* program = parse("", NULL, 0);
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(0, program->proc_count);
    free_program(program);
}

TEST(parser_parses_integer_literal) {
    Expr* expr = parse_expression("42");
    ASSERT_PTR_NOT_NULL(expr);
    ASSERT_INT_EQ(EXPR_LITERAL, expr->kind);
    ASSERT_INT_EQ(VAL_INT, expr->as.literal.value.type);
    ASSERT_INT_EQ(42, expr->as.literal.value.as.as_int);
    free_expr(expr);
}

TEST(parser_parses_float_literal) {
    Expr* expr = parse_expression("3.14");
    ASSERT_PTR_NOT_NULL(expr);
    ASSERT_INT_EQ(EXPR_LITERAL, expr->kind);
    ASSERT_INT_EQ(VAL_FLOAT, expr->as.literal.value.type);
    ASSERT_FLOAT_EQ(3.14, expr->as.literal.value.as.as_float);
    free_expr(expr);
}

TEST(parser_parses_string_literal) {
    Expr* expr = parse_expression("\"hello\"");
    ASSERT_PTR_NOT_NULL(expr);
    ASSERT_INT_EQ(EXPR_LITERAL, expr->kind);
    ASSERT_INT_EQ(VAL_STRING, expr->as.literal.value.type);
    ASSERT_STRING_EQ("hello", expr->as.literal.value.as.as_string);
    free_expr(expr);
}

TEST(parser_parses_identifier) {
    Expr* expr = parse_expression("foo");
    ASSERT_PTR_NOT_NULL(expr);
    ASSERT_INT_EQ(EXPR_VARIABLE, expr->kind);
    ASSERT_INT_EQ(0, strcmp("foo", expr->as.variable.name));
    free_expr(expr);
}

TEST(parser_parses_addition) {
    Expr* expr = parse_expression("1 + 2");
    ASSERT_PTR_NOT_NULL(expr);
    ASSERT_INT_EQ(EXPR_BINARY, expr->kind);
    ASSERT_INT_EQ(TOKEN_PLUS, expr->as.binary.op);
    free_expr(expr);
}

TEST(parser_parses_unary_minus) {
    Expr* expr = parse_expression("-5");
    ASSERT_PTR_NOT_NULL(expr);
    ASSERT_INT_EQ(EXPR_UNARY, expr->kind);
    ASSERT_INT_EQ(TOKEN_MINUS, expr->as.unary.op);
    ASSERT_INT_EQ(EXPR_LITERAL, expr->as.unary.operand->kind);
    ASSERT_INT_EQ(5, expr->as.unary.operand->as.literal.value.as.as_int);
    free_expr(expr);
}

TEST(parser_parses_unary_not) {
    Expr* expr = parse_expression("!0");
    ASSERT_PTR_NOT_NULL(expr);
    ASSERT_INT_EQ(EXPR_UNARY, expr->kind);
    ASSERT_INT_EQ(TOKEN_BANG, expr->as.unary.op);
    ASSERT_INT_EQ(EXPR_LITERAL, expr->as.unary.operand->kind);
    ASSERT_INT_EQ(0, expr->as.unary.operand->as.literal.value.as.as_int);
    free_expr(expr);
}

TEST(parser_respects_precedence) {
    Expr* expr = parse_expression("1 + 2 * 3");
    ASSERT_PTR_NOT_NULL(expr);
    ASSERT_INT_EQ(EXPR_BINARY, expr->kind);
    ASSERT_INT_EQ(TOKEN_PLUS, expr->as.binary.op);
    ASSERT_INT_EQ(EXPR_BINARY, expr->as.binary.right->kind);
    ASSERT_INT_EQ(TOKEN_STAR, expr->as.binary.right->as.binary.op);
    free_expr(expr);
}

TEST(parser_parses_comparison) {
    Expr* expr = parse_expression("x == 1");
    ASSERT_PTR_NOT_NULL(expr);
    ASSERT_INT_EQ(EXPR_BINARY, expr->kind);
    ASSERT_INT_EQ(TOKEN_EQ, expr->as.binary.op);
    free_expr(expr);
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
    Program* program = parse("proc main() -> int { int x = 42; }", NULL, 0);
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(1, program->proc_count);
    ProcDecl* proc = &program->procs[0];
    ASSERT_INT_EQ(0, strcmp("main", proc->name));
    ASSERT_INT_EQ(TYPE_INT, proc->return_type->kind);
    ASSERT_INT_EQ(1, proc->body->stmt_count);
    Stmt* stmt = proc->body->stmts[0];
    ASSERT_INT_EQ(STMT_VAR_DECL, stmt->kind);
    ASSERT_INT_EQ(TYPE_INT, stmt->as.var_decl.type->kind);
    ASSERT_INT_EQ(0, strcmp("x", stmt->as.var_decl.name));
    ASSERT_INT_EQ(EXPR_LITERAL, stmt->as.var_decl.initializer->kind);
    ASSERT_INT_EQ(42, stmt->as.var_decl.initializer->as.literal.value.as.as_int);
    free_program(program);
}

TEST(parser_parses_assignment) {
    Program* program = parse("proc main() -> int { x = 1; }", NULL, 0);
    ASSERT_PTR_NOT_NULL(program);
    Stmt* stmt = program->procs[0].body->stmts[0];
    ASSERT_INT_EQ(STMT_ASSIGN, stmt->kind);
    ASSERT_INT_EQ(0, strcmp("x", stmt->as.assign.name));
    free_program(program);
}

TEST(parser_parses_if_statement) {
    Program* program = parse("proc main() -> int { if x == 1 { return 0; } }", NULL, 0);
    ASSERT_PTR_NOT_NULL(program);
    Stmt* stmt = program->procs[0].body->stmts[0];
    ASSERT_INT_EQ(STMT_IF, stmt->kind);
    free_program(program);
}

TEST(parser_parses_while_statement) {
    Program* program = parse("proc main() -> int { while x < 10 { x = x + 1; } }", NULL, 0);
    ASSERT_PTR_NOT_NULL(program);
    Stmt* stmt = program->procs[0].body->stmts[0];
    ASSERT_INT_EQ(STMT_WHILE, stmt->kind);
    ASSERT_PTR_NOT_NULL(stmt->as.while_stmt.condition);
    ASSERT_PTR_NOT_NULL(stmt->as.while_stmt.body);
    free_program(program);
}

TEST(parser_parses_break_statement) {
    Program* program = parse("proc main() -> int { while 1 { break; } }", NULL, 0);
    ASSERT_PTR_NOT_NULL(program);
    Stmt* while_stmt = program->procs[0].body->stmts[0];
    ASSERT_INT_EQ(STMT_WHILE, while_stmt->kind);
    Stmt* body_stmt = while_stmt->as.while_stmt.body->stmts[0];
    ASSERT_INT_EQ(STMT_BREAK, body_stmt->kind);
    free_program(program);
}

TEST(parser_parses_continue_statement) {
    Program* program = parse("proc main() -> int { while 1 { continue; } }", NULL, 0);
    ASSERT_PTR_NOT_NULL(program);
    Stmt* while_stmt = program->procs[0].body->stmts[0];
    ASSERT_INT_EQ(STMT_WHILE, while_stmt->kind);
    Stmt* body_stmt = while_stmt->as.while_stmt.body->stmts[0];
    ASSERT_INT_EQ(STMT_CONTINUE, body_stmt->kind);
    free_program(program);
}

TEST(parser_parses_else_branch) {
    Program* program = parse("proc main() -> int { if 0 { return 1; } else { return 2; } }", NULL, 0);
    ASSERT_PTR_NOT_NULL(program);
    Stmt* stmt = program->procs[0].body->stmts[0];
    ASSERT_INT_EQ(STMT_IF, stmt->kind);
    ASSERT_PTR_NOT_NULL(stmt->as.if_stmt.else_block);
    ASSERT_INT_EQ(1, stmt->as.if_stmt.else_block->stmt_count);
    free_program(program);
}

TEST(parser_parses_else_if_chain) {
    Program* program = parse("proc main() -> int { if 0 { return 1; } else if 1 { return 2; } else { return 3; } }", NULL, 0);
    ASSERT_PTR_NOT_NULL(program);
    Stmt* stmt = program->procs[0].body->stmts[0];
    ASSERT_INT_EQ(STMT_IF, stmt->kind);
    ASSERT_PTR_NOT_NULL(stmt->as.if_stmt.else_block);
    ASSERT_INT_EQ(1, stmt->as.if_stmt.else_block->stmt_count);
    Stmt* nested = stmt->as.if_stmt.else_block->stmts[0];
    ASSERT_INT_EQ(STMT_IF, nested->kind);
    ASSERT_PTR_NOT_NULL(nested->as.if_stmt.else_block);
    free_program(program);
}

TEST(parser_parses_return_statement) {
    Program* program = parse("proc main() -> int { return 42; }", NULL, 0);
    ASSERT_PTR_NOT_NULL(program);
    Stmt* stmt = program->procs[0].body->stmts[0];
    ASSERT_INT_EQ(STMT_RETURN, stmt->kind);
    ASSERT_INT_EQ(EXPR_LITERAL, stmt->as.return_stmt.value->kind);
    free_program(program);
}

TEST(parser_parses_for_sql_loop) {
    Program* program = parse("proc main() -> int { for row in SELECT * FROM users { return 0; } }", NULL, 0);
    ASSERT_PTR_NOT_NULL(program);
    Stmt* stmt = program->procs[0].body->stmts[0];
    ASSERT_INT_EQ(STMT_FOR, stmt->kind);
    ASSERT_INT_EQ(0, strcmp("row", stmt->as.for_stmt.var_name));
    ASSERT_INT_EQ(0, strcmp("SELECT * FROM users", stmt->as.for_stmt.sql_query));
    free_program(program);
}

TEST(parser_parses_for_sql_loop_with_param) {
    Program* program = parse("proc main() -> int { int id = 1; for row in SELECT * FROM users WHERE id = ?id { return 0; } }", NULL, 0);
    ASSERT_PTR_NOT_NULL(program);
    Stmt* stmt = program->procs[0].body->stmts[1];
    ASSERT_INT_EQ(STMT_FOR, stmt->kind);
    ASSERT_STRING_EQ("SELECT * FROM users WHERE id = ?", stmt->as.for_stmt.sql_query);
    ASSERT_INT_EQ(1, stmt->as.for_stmt.param_count);
    ASSERT_INT_EQ(EXPR_SQL_PARAM, stmt->as.for_stmt.params[0]->kind);
    ASSERT_STRING_EQ("id", stmt->as.for_stmt.params[0]->as.sql_param.name);
    free_program(program);
}

TEST(parser_parses_call_expression) {
    Expr* expr = parse_expression("foo()");
    ASSERT_PTR_NOT_NULL(expr);
    ASSERT_INT_EQ(EXPR_CALL, expr->kind);
    ASSERT_INT_EQ(0, strcmp("foo", expr->as.call.name));
    ASSERT_INT_EQ(0, expr->as.call.arg_count);
    free_expr(expr);
}

TEST(parser_parses_procedure_parameters) {
    Program* program = parse("proc add(a int, b int) -> int { return a + b; }", NULL, 0);
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(1, program->proc_count);
    ProcDecl* proc = &program->procs[0];
    ASSERT_INT_EQ(2, proc->param_count);
    ASSERT_INT_EQ(0, strcmp("a", proc->params[0].name));
    ASSERT_INT_EQ(TYPE_INT, proc->params[0].type->kind);
    ASSERT_INT_EQ(0, strcmp("b", proc->params[1].name));
    ASSERT_INT_EQ(TYPE_INT, proc->params[1].type->kind);
    free_program(program);
}

TEST(parser_reports_error_for_missing_brace) {
    char error[256];
    Program* program = parse("proc main() -> int { return 1; ", error, sizeof(error));
    ASSERT_PTR_NULL(program);
    ASSERT_PTR_NOT_NULL(strstr(error, "line"));
}

TEST(parser_parses_bool_literal_true) {
    Expr* expr = parse_expression("true");
    ASSERT_PTR_NOT_NULL(expr);
    ASSERT_INT_EQ(EXPR_LITERAL, expr->kind);
    ASSERT_INT_EQ(VAL_BOOL, expr->as.literal.value.type);
    ASSERT_INT_EQ(1, expr->as.literal.value.as.as_int);
    free_expr(expr);
}

TEST(parser_parses_bool_literal_false) {
    Expr* expr = parse_expression("false");
    ASSERT_PTR_NOT_NULL(expr);
    ASSERT_INT_EQ(EXPR_LITERAL, expr->kind);
    ASSERT_INT_EQ(VAL_BOOL, expr->as.literal.value.type);
    ASSERT_INT_EQ(0, expr->as.literal.value.as.as_int);
    free_expr(expr);
}

TEST(parser_parses_array_literal) {
    Program* program = parse("proc main() -> array { array a = [1, 2, 3]; return a; }", NULL, 0);
    ASSERT_PTR_NOT_NULL(program);
    Stmt* stmt = program->procs[0].body->stmts[0];
    ASSERT_INT_EQ(STMT_VAR_DECL, stmt->kind);
    ASSERT_INT_EQ(TYPE_ARRAY, stmt->as.var_decl.type->kind);
    ASSERT_INT_EQ(EXPR_ARRAY, stmt->as.var_decl.initializer->kind);
    ASSERT_INT_EQ(3, stmt->as.var_decl.initializer->as.array.count);
    free_program(program);
}

TEST(parser_parses_index_expression) {
    Expr* expr = parse_expression("a[0]");
    ASSERT_PTR_NOT_NULL(expr);
    ASSERT_INT_EQ(EXPR_INDEX, expr->kind);
    ASSERT_INT_EQ(EXPR_VARIABLE, expr->as.index.array->kind);
    ASSERT_INT_EQ(EXPR_LITERAL, expr->as.index.index->kind);
    free_expr(expr);
}

TEST(parser_parses_index_assignment) {
    Program* program = parse("proc main() -> int { a[0] = 1; }", NULL, 0);
    ASSERT_PTR_NOT_NULL(program);
    Stmt* stmt = program->procs[0].body->stmts[0];
    ASSERT_INT_EQ(STMT_INDEX_ASSIGN, stmt->kind);
    ASSERT_INT_EQ(EXPR_VARIABLE, stmt->as.index_assign.array->kind);
    free_program(program);
}

TEST(parser_parses_nested_index_assignment) {
    Program* program = parse("proc main() -> int { a[0][1] = 2; }", NULL, 0);
    ASSERT_PTR_NOT_NULL(program);
    Stmt* stmt = program->procs[0].body->stmts[0];
    ASSERT_INT_EQ(STMT_INDEX_ASSIGN, stmt->kind);
    ASSERT_INT_EQ(EXPR_INDEX, stmt->as.index_assign.array->kind);
    ASSERT_INT_EQ(EXPR_VARIABLE, stmt->as.index_assign.array->as.index.array->kind);
    ASSERT_INT_EQ(EXPR_LITERAL, stmt->as.index_assign.array->as.index.index->kind);
    ASSERT_INT_EQ(EXPR_LITERAL, stmt->as.index_assign.index->kind);
    free_program(program);
}

TEST(ast_sql_stmt_node_exists) {
    Stmt* stmt = create_sql_stmt(STMT_SQL_DDL, "CREATE TABLE t (id int)", NULL, 0, NULL, 0);
    ASSERT_PTR_NOT_NULL(stmt);
    ASSERT_INT_EQ(STMT_SQL_DDL, stmt->kind);
    free_stmt(stmt);
}

TEST(parser_parses_create_table) {
    Program* program = parse("proc main() -> int { create table t (id int); return 0; }", NULL, 0);
    ASSERT_PTR_NOT_NULL(program);
    Stmt* stmt = program->procs[0].body->stmts[0];
    ASSERT_INT_EQ(STMT_SQL_DDL, stmt->kind);
    ASSERT_STRING_EQ("create table t (id int)", stmt->as.sql_stmt.sql);
    free_program(program);
}

TEST(parser_parses_insert_with_param) {
    Program* program = parse("proc main() -> int { int x = 1; insert into t values (?x); return 0; }", NULL, 0);
    ASSERT_PTR_NOT_NULL(program);
    Stmt* stmt = program->procs[0].body->stmts[1];
    ASSERT_INT_EQ(STMT_SQL_DML, stmt->kind);
    ASSERT_STRING_EQ("insert into t values (?)", stmt->as.sql_stmt.sql);
    ASSERT_INT_EQ(1, stmt->as.sql_stmt.param_count);
    ASSERT_INT_EQ(EXPR_SQL_PARAM, stmt->as.sql_stmt.params[0]->kind);
    ASSERT_STRING_EQ("x", stmt->as.sql_stmt.params[0]->as.sql_param.name);
    free_program(program);
}

TEST(parser_parses_select_into_single_value) {
    Program* program = parse("proc main() -> int { string name; SELECT name INTO my_name FROM users WHERE id = 1; return 0; }", NULL, 0);
    ASSERT_PTR_NOT_NULL(program);
    Stmt* stmt = program->procs[0].body->stmts[1];
    ASSERT_INT_EQ(STMT_SQL_QUERY, stmt->kind);
    ASSERT_STRING_EQ("SELECT name FROM users WHERE id = 1", stmt->as.sql_stmt.sql);
    ASSERT_INT_EQ(1, stmt->as.sql_stmt.into_count);
    ASSERT_STRING_EQ("my_name", stmt->as.sql_stmt.into_vars[0]);
    free_program(program);
}

TEST(parser_parses_select_into_multi_value) {
    Program* program = parse("proc main() -> int { int a; int b; SELECT id, name INTO a, b FROM users WHERE id = 1; return 0; }", NULL, 0);
    ASSERT_PTR_NOT_NULL(program);
    Stmt* stmt = program->procs[0].body->stmts[2];
    ASSERT_INT_EQ(STMT_SQL_QUERY, stmt->kind);
    ASSERT_STRING_EQ("SELECT id, name FROM users WHERE id = 1", stmt->as.sql_stmt.sql);
    ASSERT_INT_EQ(2, stmt->as.sql_stmt.into_count);
    ASSERT_STRING_EQ("a", stmt->as.sql_stmt.into_vars[0]);
    ASSERT_STRING_EQ("b", stmt->as.sql_stmt.into_vars[1]);
    free_program(program);
}

TEST(parser_parses_select_into_array_row) {
    Program* program = parse("proc main() -> int { array<row> rows; SELECT * INTO rows FROM users; return 0; }", NULL, 0);
    ASSERT_PTR_NOT_NULL(program);
    Stmt* stmt = program->procs[0].body->stmts[1];
    ASSERT_INT_EQ(STMT_SQL_QUERY, stmt->kind);
    ASSERT_STRING_EQ("SELECT * FROM users", stmt->as.sql_stmt.sql);
    ASSERT_INT_EQ(1, stmt->as.sql_stmt.into_count);
    ASSERT_STRING_EQ("rows", stmt->as.sql_stmt.into_vars[0]);
    free_program(program);
}

TEST(parser_parses_import_statement) {
    Program* program = parse("import \"foo.mypl\"; proc main() -> int { return 0; }", NULL, 0);
    ASSERT_PTR_NOT_NULL(program);
    ASSERT_INT_EQ(1, program->import_count);
    ASSERT_INT_EQ(STMT_IMPORT, program->imports[0]->kind);
    ASSERT_STRING_EQ("foo.mypl", program->imports[0]->as.import_stmt.path);
    free_program(program);
}

TEST(parser_parses_typed_array_variable) {
    char error[256];
    Program* program = parse("proc main() -> int { array<int> a = [1, 2]; return 0; }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    free_program(program);
}

TEST(parser_parses_nested_typed_array) {
    char error[256];
    Program* program = parse("proc main() -> int { array<array<int>> a = []; return 0; }", error, sizeof(error));
    ASSERT_PTR_NOT_NULL(program);
    free_program(program);
}

int main(void) {
    RUN_TEST(parser_returns_empty_program_for_empty_source);
    RUN_TEST(parser_parses_integer_literal);
    RUN_TEST(parser_parses_float_literal);
    RUN_TEST(parser_parses_string_literal);
    RUN_TEST(parser_parses_identifier);
    RUN_TEST(parser_parses_addition);
    RUN_TEST(parser_parses_unary_minus);
    RUN_TEST(parser_parses_unary_not);
    RUN_TEST(parser_respects_precedence);
    RUN_TEST(parser_parses_comparison);
    RUN_TEST(parser_parses_field_expression);
    RUN_TEST(parser_parses_procedure_and_var_decl);
    RUN_TEST(parser_parses_assignment);
    RUN_TEST(parser_parses_if_statement);
    RUN_TEST(parser_parses_while_statement);
    RUN_TEST(parser_parses_break_statement);
    RUN_TEST(parser_parses_continue_statement);
    RUN_TEST(parser_parses_else_branch);
    RUN_TEST(parser_parses_else_if_chain);
    RUN_TEST(parser_parses_return_statement);
    RUN_TEST(parser_parses_for_sql_loop);
    RUN_TEST(parser_parses_for_sql_loop_with_param);
    RUN_TEST(parser_parses_call_expression);
    RUN_TEST(parser_parses_procedure_parameters);
    RUN_TEST(parser_reports_error_for_missing_brace);
    RUN_TEST(parser_parses_bool_literal_true);
    RUN_TEST(parser_parses_bool_literal_false);
    RUN_TEST(parser_parses_array_literal);
    RUN_TEST(parser_parses_index_expression);
    RUN_TEST(parser_parses_index_assignment);
    RUN_TEST(parser_parses_nested_index_assignment);
    RUN_TEST(ast_sql_stmt_node_exists);
    RUN_TEST(parser_parses_create_table);
    RUN_TEST(parser_parses_insert_with_param);
    RUN_TEST(parser_parses_select_into_single_value);
    RUN_TEST(parser_parses_select_into_multi_value);
    RUN_TEST(parser_parses_select_into_array_row);
    RUN_TEST(parser_parses_import_statement);
    RUN_TEST(parser_parses_typed_array_variable);
    RUN_TEST(parser_parses_nested_typed_array);
    TEST_SUMMARY();
}
