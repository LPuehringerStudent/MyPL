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
    if (program == NULL) return NULL;
    program->procs = NULL;
    program->proc_count = 0;
    return program;
}

void free_expr(Expr* expr) {
    if (expr == NULL) return;
    switch (expr->kind) {
        case EXPR_VARIABLE:
            free(expr->as.variable.name);
            break;
        case EXPR_BINARY:
            free_expr(expr->as.binary.left);
            free_expr(expr->as.binary.right);
            break;
        case EXPR_UNARY:
            free_expr(expr->as.unary.operand);
            break;
        case EXPR_FIELD:
            free(expr->as.field.row);
            free(expr->as.field.field);
            break;
        case EXPR_CALL:
            free(expr->as.call.name);
            for (int i = 0; i < expr->as.call.arg_count; i++) {
                free_expr(expr->as.call.args[i]);
            }
            free(expr->as.call.args);
            break;
        default:
            break;
    }
    free(expr);
}

static void free_stmt(Stmt* stmt);

void free_block(Block* block) {
    if (block == NULL) return;
    for (int i = 0; i < block->stmt_count; i++) {
        free_stmt(block->stmts[i]);
    }
    free(block->stmts);
    free(block);
}

ProcDecl* create_proc_decl(const char* name, TypeKind return_type) {
    ProcDecl* proc = malloc(sizeof(ProcDecl));
    if (proc == NULL) return NULL;
    proc->name = copy_string(name);
    if (proc->name == NULL && name != NULL) {
        free(proc);
        return NULL;
    }
    proc->params = NULL;
    proc->param_count = 0;
    proc->return_type = return_type;
    proc->body = NULL;
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
    if (block == NULL) return NULL;
    block->stmts = NULL;
    block->stmt_count = 0;
    return block;
}

static void free_stmt(Stmt* stmt) {
    if (stmt == NULL) return;
    switch (stmt->kind) {
        case STMT_VAR_DECL:
            free(stmt->as.var_decl.name);
            free_expr(stmt->as.var_decl.initializer);
            break;
        case STMT_ASSIGN:
            free(stmt->as.assign.name);
            free_expr(stmt->as.assign.value);
            break;
        case STMT_IF:
            free_expr(stmt->as.if_stmt.condition);
            free_block(stmt->as.if_stmt.then_block);
            free_block(stmt->as.if_stmt.else_block);
            break;
        case STMT_FOR:
            free(stmt->as.for_stmt.var_name);
            free(stmt->as.for_stmt.sql_query);
            free_block(stmt->as.for_stmt.body);
            break;
        case STMT_RETURN:
            free_expr(stmt->as.return_stmt.value);
            break;
        case STMT_PRINT:
            free_expr(stmt->as.print_stmt.value);
            break;
    }
    free(stmt);
}

Stmt* create_var_decl_stmt(TypeKind type, const char* name, Expr* init) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (stmt == NULL) return NULL;
    stmt->kind = STMT_VAR_DECL;
    stmt->as.var_decl.type = type;
    stmt->as.var_decl.name = copy_string(name);
    if (stmt->as.var_decl.name == NULL && name != NULL) {
        free(stmt);
        return NULL;
    }
    stmt->as.var_decl.initializer = init;
    return stmt;
}

Stmt* create_assign_stmt(const char* name, Expr* value) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (stmt == NULL) return NULL;
    stmt->kind = STMT_ASSIGN;
    stmt->as.assign.name = copy_string(name);
    if (stmt->as.assign.name == NULL && name != NULL) {
        free(stmt);
        return NULL;
    }
    stmt->as.assign.value = value;
    return stmt;
}

Stmt* create_if_stmt(Expr* cond, Block* then_block, Block* else_block) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (stmt == NULL) {
        free_expr(cond);
        free_block(then_block);
        free_block(else_block);
        return NULL;
    }
    stmt->kind = STMT_IF;
    stmt->as.if_stmt.condition = cond;
    stmt->as.if_stmt.then_block = then_block;
    stmt->as.if_stmt.else_block = else_block;
    return stmt;
}

Stmt* create_for_stmt(const char* var_name, const char* sql_query, Block* body) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (stmt == NULL) {
        free_block(body);
        return NULL;
    }
    stmt->kind = STMT_FOR;
    stmt->as.for_stmt.var_name = copy_string(var_name);
    if (stmt->as.for_stmt.var_name == NULL && var_name != NULL) {
        free_block(body);
        free(stmt);
        return NULL;
    }
    stmt->as.for_stmt.sql_query = copy_string(sql_query);
    if (stmt->as.for_stmt.sql_query == NULL && sql_query != NULL) {
        free(stmt->as.for_stmt.var_name);
        free_block(body);
        free(stmt);
        return NULL;
    }
    stmt->as.for_stmt.body = body;
    return stmt;
}

Stmt* create_return_stmt(Expr* value) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (stmt == NULL) {
        free_expr(value);
        return NULL;
    }
    stmt->kind = STMT_RETURN;
    stmt->as.return_stmt.value = value;
    return stmt;
}

Stmt* create_print_stmt(Expr* value) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (stmt == NULL) {
        free_expr(value);
        return NULL;
    }
    stmt->kind = STMT_PRINT;
    stmt->as.print_stmt.value = value;
    return stmt;
}

Expr* create_literal_expr(Value value) {
    Expr* expr = malloc(sizeof(Expr));
    if (expr == NULL) return NULL;
    expr->kind = EXPR_LITERAL;
    expr->as.literal.value = value;
    return expr;
}

Expr* create_variable_expr(const char* name) {
    Expr* expr = malloc(sizeof(Expr));
    if (expr == NULL) return NULL;
    expr->kind = EXPR_VARIABLE;
    expr->as.variable.name = copy_string(name);
    if (expr->as.variable.name == NULL && name != NULL) {
        free(expr);
        return NULL;
    }
    return expr;
}

Expr* create_binary_expr(TokenType op, Expr* left, Expr* right) {
    Expr* expr = malloc(sizeof(Expr));
    if (expr == NULL) {
        free_expr(left);
        free_expr(right);
        return NULL;
    }
    expr->kind = EXPR_BINARY;
    expr->as.binary.op = op;
    expr->as.binary.left = left;
    expr->as.binary.right = right;
    return expr;
}

Expr* create_unary_expr(TokenType op, Expr* operand) {
    Expr* expr = malloc(sizeof(Expr));
    if (expr == NULL) {
        free_expr(operand);
        return NULL;
    }
    expr->kind = EXPR_UNARY;
    expr->as.unary.op = op;
    expr->as.unary.operand = operand;
    return expr;
}

Expr* create_field_expr(const char* row, const char* field) {
    Expr* expr = malloc(sizeof(Expr));
    if (expr == NULL) return NULL;
    expr->kind = EXPR_FIELD;
    expr->as.field.row = copy_string(row);
    if (expr->as.field.row == NULL && row != NULL) {
        free(expr);
        return NULL;
    }
    expr->as.field.field = copy_string(field);
    if (expr->as.field.field == NULL && field != NULL) {
        free(expr->as.field.row);
        free(expr);
        return NULL;
    }
    return expr;
}

Expr* create_call_expr(const char* name, Expr** args, int arg_count) {
    Expr* expr = malloc(sizeof(Expr));
    if (expr == NULL) {
        for (int i = 0; i < arg_count; i++) {
            free_expr(args[i]);
        }
        free(args);
        return NULL;
    }
    expr->kind = EXPR_CALL;
    expr->as.call.name = copy_string(name);
    if (expr->as.call.name == NULL && name != NULL) {
        for (int i = 0; i < arg_count; i++) {
            free_expr(args[i]);
        }
        free(args);
        free(expr);
        return NULL;
    }
    expr->as.call.args = args;
    expr->as.call.arg_count = arg_count;
    return expr;
}
