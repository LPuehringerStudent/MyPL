#include <stdlib.h>
#include <string.h>

#include "ast.h"

Type type_int      = { TYPE_INT,    NULL };
Type type_float    = { TYPE_FLOAT,  NULL };
Type type_string   = { TYPE_STRING, NULL };
Type type_bool     = { TYPE_BOOL,   NULL };
Type type_row      = { TYPE_ROW,    NULL };
Type type_unknown  = { TYPE_UNKNOWN, NULL };

Type* type_new(TypeKind kind, Type* element_type) {
    Type* t = malloc(sizeof(Type));
    if (t == NULL) return NULL;
    t->kind = kind;
    t->element_type = element_type;
    return t;
}

Type* type_copy(Type* t) {
    if (t == NULL) return NULL;
    if (t == &type_int || t == &type_float || t == &type_string ||
        t == &type_bool || t == &type_row || t == &type_unknown) {
        return t;
    }
    return type_new(t->kind, type_copy(t->element_type));
}

void type_free(Type* t) {
    if (t == NULL) return;
    if (t == &type_int || t == &type_float || t == &type_string ||
        t == &type_bool || t == &type_row || t == &type_unknown) {
        return;
    }
    type_free(t->element_type);
    free(t);
}

int type_equals(Type* a, Type* b) {
    if (a == b) return 1;
    if (a == NULL || b == NULL) return 0;
    if (a->kind != b->kind) return 0;
    if (a->kind == TYPE_ARRAY) return type_equals(a->element_type, b->element_type);
    return 1;
}

int type_is_numeric(Type* t) {
    return t != NULL && t != &type_unknown &&
           (t->kind == TYPE_INT || t->kind == TYPE_FLOAT);
}
int type_is_array(Type* t) {
    return t != NULL && t != &type_unknown && t->kind == TYPE_ARRAY;
}
int type_is_unknown(Type* t) { return t == &type_unknown; }

const char* type_name(Type* t) {
    if (t == NULL || t == &type_unknown) return "unknown";
    switch (t->kind) {
        case TYPE_INT:    return "int";
        case TYPE_FLOAT:  return "float";
        case TYPE_STRING: return "string";
        case TYPE_BOOL:   return "bool";
        case TYPE_ARRAY:  return "array";
        case TYPE_ROW:    return "row";
        case TYPE_UNKNOWN: return "unknown";
    }
    return "unknown";
}

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
    program->imports = NULL;
    program->import_count = 0;
    program->import_capacity = 0;
    program->procs = NULL;
    program->proc_count = 0;
    return program;
}

void free_expr(Expr* expr) {
    if (expr == NULL) return;
    switch (expr->kind) {
        case EXPR_LITERAL:
            value_release(expr->as.literal.value);
            break;
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
        case EXPR_ARRAY:
            for (int i = 0; i < expr->as.array.count; i++) {
                free_expr(expr->as.array.elements[i]);
            }
            free(expr->as.array.elements);
            break;
        case EXPR_INDEX:
            free_expr(expr->as.index.array);
            free_expr(expr->as.index.index);
            break;
        case EXPR_SQL_PARAM:
            free(expr->as.sql_param.name);
            break;
        case EXPR_ROW_FIELD:
            free_expr(expr->as.row_field.row);
            free(expr->as.row_field.field);
            break;
        default:
            break;
    }
    free(expr);
}

void free_stmt(Stmt* stmt);

void free_block(Block* block) {
    if (block == NULL) return;
    for (int i = 0; i < block->stmt_count; i++) {
        free_stmt(block->stmts[i]);
    }
    free(block->stmts);
    free(block);
}

ProcDecl* create_proc_decl(const char* name, Type* return_type) {
    ProcDecl* proc = malloc(sizeof(ProcDecl));
    if (proc == NULL) {
        type_free(return_type);
        return NULL;
    }
    proc->name = copy_string(name);
    if (proc->name == NULL && name != NULL) {
        type_free(return_type);
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
    for (int i = 0; i < program->import_count; i++) {
        free_stmt(program->imports[i]);
    }
    free(program->imports);
    for (int i = 0; i < program->proc_count; i++) {
        free(program->procs[i].name);
        free_block(program->procs[i].body);
        for (int j = 0; j < program->procs[i].param_count; j++) {
            free(program->procs[i].params[j].name);
            type_free(program->procs[i].params[j].type);
        }
        free(program->procs[i].params);
        type_free(program->procs[i].return_type);
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

void free_stmt(Stmt* stmt) {
    if (stmt == NULL) return;
    switch (stmt->kind) {
        case STMT_VAR_DECL:
            type_free(stmt->as.var_decl.type);
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
            for (int i = 0; i < stmt->as.for_stmt.param_count; i++) {
                free_expr(stmt->as.for_stmt.params[i]);
            }
            free(stmt->as.for_stmt.params);
            free_block(stmt->as.for_stmt.body);
            break;
        case STMT_RETURN:
            free_expr(stmt->as.return_stmt.value);
            break;
        case STMT_PRINT:
            free_expr(stmt->as.print_stmt.value);
            break;
        case STMT_INDEX_ASSIGN:
            free_expr(stmt->as.index_assign.array);
            free_expr(stmt->as.index_assign.index);
            free_expr(stmt->as.index_assign.value);
            break;
        case STMT_EXPR:
            free_expr(stmt->as.expr_stmt.value);
            break;
        case STMT_IMPORT:
            free(stmt->as.import_stmt.path);
            break;
        case STMT_SQL_DDL:
        case STMT_SQL_DML:
        case STMT_SQL_QUERY:
            free(stmt->as.sql_stmt.sql);
            for (int i = 0; i < stmt->as.sql_stmt.param_count; i++) {
                free_expr(stmt->as.sql_stmt.params[i]);
            }
            free(stmt->as.sql_stmt.params);
            for (int i = 0; i < stmt->as.sql_stmt.into_count; i++) {
                free(stmt->as.sql_stmt.into_vars[i]);
            }
            free(stmt->as.sql_stmt.into_vars);
            break;
        case STMT_SQL_TRANSACTION:
            break;
    }
    free(stmt);
}

Stmt* create_var_decl_stmt(Type* type, const char* name, Expr* init) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (stmt == NULL) {
        type_free(type);
        free_expr(init);
        return NULL;
    }
    stmt->loc = (SourceLoc){0, 0};
    stmt->kind = STMT_VAR_DECL;
    stmt->as.var_decl.type = type;
    stmt->as.var_decl.name = copy_string(name);
    if (stmt->as.var_decl.name == NULL && name != NULL) {
        type_free(type);
        free_expr(init);
        free(stmt);
        return NULL;
    }
    stmt->as.var_decl.initializer = init;
    return stmt;
}

Stmt* create_assign_stmt(const char* name, Expr* value) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (stmt == NULL) return NULL;
    stmt->loc = (SourceLoc){0, 0};
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
    stmt->loc = (SourceLoc){0, 0};
    stmt->kind = STMT_IF;
    stmt->as.if_stmt.condition = cond;
    stmt->as.if_stmt.then_block = then_block;
    stmt->as.if_stmt.else_block = else_block;
    return stmt;
}

Stmt* create_for_stmt(const char* var_name, const char* sql_query, Expr** params, int param_count, Block* body) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (stmt == NULL) {
        for (int i = 0; i < param_count; i++) free_expr(params[i]);
        free(params);
        free_block(body);
        return NULL;
    }
    stmt->loc = (SourceLoc){0, 0};
    stmt->kind = STMT_FOR;
    stmt->as.for_stmt.var_name = copy_string(var_name);
    if (stmt->as.for_stmt.var_name == NULL && var_name != NULL) {
        for (int i = 0; i < param_count; i++) free_expr(params[i]);
        free(params);
        free_block(body);
        free(stmt);
        return NULL;
    }
    stmt->as.for_stmt.sql_query = copy_string(sql_query);
    if (stmt->as.for_stmt.sql_query == NULL && sql_query != NULL) {
        for (int i = 0; i < param_count; i++) free_expr(params[i]);
        free(params);
        free(stmt->as.for_stmt.var_name);
        free_block(body);
        free(stmt);
        return NULL;
    }
    stmt->as.for_stmt.params = params;
    stmt->as.for_stmt.param_count = param_count;
    stmt->as.for_stmt.body = body;
    return stmt;
}

Stmt* create_return_stmt(Expr* value) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (stmt == NULL) {
        free_expr(value);
        return NULL;
    }
    stmt->loc = (SourceLoc){0, 0};
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
    stmt->loc = (SourceLoc){0, 0};
    stmt->kind = STMT_PRINT;
    stmt->as.print_stmt.value = value;
    return stmt;
}

Stmt* create_expr_stmt(Expr* value) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (stmt == NULL) {
        free_expr(value);
        return NULL;
    }
    stmt->loc = (SourceLoc){0, 0};
    stmt->kind = STMT_EXPR;
    stmt->as.expr_stmt.value = value;
    return stmt;
}

Stmt* create_import_stmt(const char* path) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (stmt == NULL) return NULL;
    stmt->loc = (SourceLoc){0, 0};
    stmt->kind = STMT_IMPORT;
    stmt->as.import_stmt.path = copy_string(path);
    if (stmt->as.import_stmt.path == NULL && path != NULL) {
        free(stmt);
        return NULL;
    }
    return stmt;
}

Expr* create_literal_expr(Value value) {
    Expr* expr = malloc(sizeof(Expr));
    if (expr == NULL) return NULL;
    expr->loc = (SourceLoc){0, 0};
    expr->kind = EXPR_LITERAL;
    expr->as.literal.value = value;
    return expr;
}

Expr* create_variable_expr(const char* name) {
    Expr* expr = malloc(sizeof(Expr));
    if (expr == NULL) return NULL;
    expr->loc = (SourceLoc){0, 0};
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
    expr->loc = (SourceLoc){0, 0};
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
    expr->loc = (SourceLoc){0, 0};
    expr->kind = EXPR_UNARY;
    expr->as.unary.op = op;
    expr->as.unary.operand = operand;
    return expr;
}

Expr* create_field_expr(const char* row, const char* field) {
    Expr* expr = malloc(sizeof(Expr));
    if (expr == NULL) return NULL;
    expr->loc = (SourceLoc){0, 0};
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
    expr->loc = (SourceLoc){0, 0};
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

Expr* create_array_expr(Expr** elements, int count) {
    Expr* expr = malloc(sizeof(Expr));
    if (expr == NULL) {
        for (int i = 0; i < count; i++) {
            free_expr(elements[i]);
        }
        free(elements);
        return NULL;
    }
    expr->loc = (SourceLoc){0, 0};
    expr->kind = EXPR_ARRAY;
    expr->as.array.elements = elements;
    expr->as.array.count = count;
    return expr;
}

Expr* create_index_expr(Expr* array, Expr* index) {
    Expr* expr = malloc(sizeof(Expr));
    if (expr == NULL) {
        free_expr(array);
        free_expr(index);
        return NULL;
    }
    expr->loc = (SourceLoc){0, 0};
    expr->kind = EXPR_INDEX;
    expr->as.index.array = array;
    expr->as.index.index = index;
    return expr;
}

Stmt* create_index_assign_stmt(Expr* array, Expr* index, Expr* value) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (stmt == NULL) {
        free_expr(array);
        free_expr(index);
        free_expr(value);
        return NULL;
    }
    stmt->loc = (SourceLoc){0, 0};
    stmt->kind = STMT_INDEX_ASSIGN;
    stmt->as.index_assign.array = array;
    stmt->as.index_assign.index = index;
    stmt->as.index_assign.value = value;
    return stmt;
}

Stmt* create_sql_stmt(int kind, char* sql, Expr** params, int param_count, char** into_vars, int into_count) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (stmt == NULL) {
        free(sql);
        for (int i = 0; i < param_count; i++) {
            free_expr(params[i]);
        }
        free(params);
        for (int i = 0; i < into_count; i++) {
            free(into_vars[i]);
        }
        free(into_vars);
        return NULL;
    }
    stmt->loc = (SourceLoc){0, 0};
    stmt->kind = kind;
    stmt->as.sql_stmt.sql = copy_string(sql);
    stmt->as.sql_stmt.params = params;
    stmt->as.sql_stmt.param_count = param_count;
    stmt->as.sql_stmt.into_vars = into_vars;
    stmt->as.sql_stmt.into_count = into_count;
    return stmt;
}

Stmt* create_sql_transaction_stmt(int kind) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (stmt == NULL) return NULL;
    stmt->loc = (SourceLoc){0, 0};
    stmt->kind = STMT_SQL_TRANSACTION;
    stmt->as.sql_transaction.kind = kind;
    return stmt;
}

Expr* create_sql_param_expr(const char* name) {
    Expr* expr = malloc(sizeof(Expr));
    if (expr == NULL) return NULL;
    expr->loc = (SourceLoc){0, 0};
    expr->kind = EXPR_SQL_PARAM;
    expr->as.sql_param.name = copy_string(name);
    if (expr->as.sql_param.name == NULL && name != NULL) {
        free(expr);
        return NULL;
    }
    return expr;
}

Expr* create_row_field_expr(Expr* row, const char* field) {
    Expr* expr = malloc(sizeof(Expr));
    if (expr == NULL) {
        free_expr(row);
        return NULL;
    }
    expr->loc = (SourceLoc){0, 0};
    expr->kind = EXPR_ROW_FIELD;
    expr->as.row_field.row = row;
    expr->as.row_field.field = copy_string(field);
    if (expr->as.row_field.field == NULL && field != NULL) {
        free_expr(row);
        free(expr);
        return NULL;
    }
    return expr;
}
