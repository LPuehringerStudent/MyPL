#include <stdlib.h>
#include <string.h>

#include "ast.h"

static char* copy_string(const char* source) {
    if (source == NULL) return NULL;
    size_t len = strlen(source);
    char* dest = malloc(len + 1);
    if (dest) strcpy(dest, source);
    return dest;
}

Program* create_program(void) {
    Program* program = malloc(sizeof(Program));
    program->procs = NULL;
    program->proc_count = 0;
    return program;
}

static void free_expr(Expr* expr);

static void free_block(Block* block) {
    if (block == NULL) return;
    for (int i = 0; i < block->stmt_count; i++) {
        /* TODO: free_stmt */
    }
    free(block->stmts);
    free(block);
}

ProcDecl* create_proc_decl(const char* name, TypeKind return_type) {
    ProcDecl* proc = malloc(sizeof(ProcDecl));
    proc->name = copy_string(name);
    proc->params = NULL;
    proc->param_count = 0;
    proc->return_type = return_type;
    proc->body = create_block();
    return proc;
}

void free_program(Program* program) {
    if (program == NULL) return;
    for (int i = 0; i < program->proc_count; i++) {
        free(program->procs[i].name);
        free_block(program->procs[i].body);
        free(program->procs[i].params);
    }
    free(program->procs);
    free(program);
}

Block* create_block(void) {
    Block* block = malloc(sizeof(Block));
    block->stmts = NULL;
    block->stmt_count = 0;
    return block;
}

Stmt* create_var_decl_stmt(TypeKind type, const char* name, Expr* init) {
    Stmt* stmt = malloc(sizeof(Stmt));
    stmt->kind = STMT_VAR_DECL;
    stmt->as.var_decl.type = type;
    stmt->as.var_decl.name = copy_string(name);
    stmt->as.var_decl.initializer = init;
    return stmt;
}

Stmt* create_assign_stmt(const char* name, Expr* value) {
    Stmt* stmt = malloc(sizeof(Stmt));
    stmt->kind = STMT_ASSIGN;
    stmt->as.assign.name = copy_string(name);
    stmt->as.assign.value = value;
    return stmt;
}

Stmt* create_if_stmt(Expr* cond, Block* then_block, Block* else_block) {
    Stmt* stmt = malloc(sizeof(Stmt));
    stmt->kind = STMT_IF;
    stmt->as.if_stmt.condition = cond;
    stmt->as.if_stmt.then_block = then_block;
    stmt->as.if_stmt.else_block = else_block;
    return stmt;
}

Stmt* create_for_stmt(const char* var_name, const char* sql_query, Block* body) {
    Stmt* stmt = malloc(sizeof(Stmt));
    stmt->kind = STMT_FOR;
    stmt->as.for_stmt.var_name = copy_string(var_name);
    stmt->as.for_stmt.sql_query = copy_string(sql_query);
    stmt->as.for_stmt.body = body;
    return stmt;
}

Stmt* create_return_stmt(Expr* value) {
    Stmt* stmt = malloc(sizeof(Stmt));
    stmt->kind = STMT_RETURN;
    stmt->as.return_stmt.value = value;
    return stmt;
}

Expr* create_literal_expr(Value value) {
    Expr* expr = malloc(sizeof(Expr));
    expr->kind = EXPR_LITERAL;
    expr->as.literal.value = value;
    return expr;
}

Expr* create_variable_expr(const char* name) {
    Expr* expr = malloc(sizeof(Expr));
    expr->kind = EXPR_VARIABLE;
    expr->as.variable.name = copy_string(name);
    return expr;
}

Expr* create_binary_expr(TokenType op, Expr* left, Expr* right) {
    Expr* expr = malloc(sizeof(Expr));
    expr->kind = EXPR_BINARY;
    expr->as.binary.op = op;
    expr->as.binary.left = left;
    expr->as.binary.right = right;
    return expr;
}

Expr* create_unary_expr(TokenType op, Expr* operand) {
    Expr* expr = malloc(sizeof(Expr));
    expr->kind = EXPR_UNARY;
    expr->as.unary.op = op;
    expr->as.unary.operand = operand;
    return expr;
}

Expr* create_field_expr(const char* row, const char* field) {
    Expr* expr = malloc(sizeof(Expr));
    expr->kind = EXPR_FIELD;
    expr->as.field.row = copy_string(row);
    expr->as.field.field = copy_string(field);
    return expr;
}
