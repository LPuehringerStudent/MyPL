#include <stdlib.h>
#include <string.h>

#include "ast.h"

static char* copy_string(const char* source);

Type type_int      = { .kind = TYPE_INT    };
Type type_float    = { .kind = TYPE_FLOAT  };
Type type_string   = { .kind = TYPE_STRING };
Type type_bool     = { .kind = TYPE_BOOL   };
Type type_row      = { .kind = TYPE_ROW    };
Type type_cursor   = { .kind = TYPE_CURSOR };
Type type_unknown  = { .kind = TYPE_UNKNOWN};

Type* type_new(TypeKind kind, Type* element_type) {
    Type* t = calloc(1, sizeof(Type));
    if (t == NULL) return NULL;
    t->kind = kind;
    t->element_type = element_type;
    return t;
}

Type* type_new_map(Type* key_type, Type* value_type) {
    Type* t = calloc(1, sizeof(Type));
    if (t == NULL) return NULL;
    t->kind = TYPE_MAP;
    t->map_key_type = key_type;
    t->element_type = value_type;
    return t;
}

Type* type_copy(Type* t) {
    if (t == NULL) return NULL;
    if (t == &type_int || t == &type_float || t == &type_string ||
        t == &type_bool || t == &type_row || t == &type_cursor ||
        t == &type_unknown) {
        return t;
    }
    if (t->kind == TYPE_STRUCT) {
        Type* copy = type_new(TYPE_STRUCT, NULL);
        if (copy == NULL) return NULL;
        copy->struct_name = copy_string(t->struct_name);
        copy->field_count = t->field_count;
        if (t->field_count > 0) {
            copy->field_names = malloc(sizeof(char*) * (size_t)t->field_count);
            copy->field_types = malloc(sizeof(Type*) * (size_t)t->field_count);
            if (copy->field_names == NULL || copy->field_types == NULL) {
                type_free(copy);
                return NULL;
            }
            for (int i = 0; i < t->field_count; i++) {
                copy->field_names[i] = copy_string(t->field_names[i]);
                copy->field_types[i] = type_copy(t->field_types[i]);
            }
        }
        return copy;
    }
    if (t->kind == TYPE_MAP) {
        return type_new_map(type_copy(t->map_key_type), type_copy(t->element_type));
    }
    return type_new(t->kind, type_copy(t->element_type));
}

void type_free(Type* t) {
    if (t == NULL) return;
    if (t == &type_int || t == &type_float || t == &type_string ||
        t == &type_bool || t == &type_row || t == &type_cursor ||
        t == &type_unknown) {
        return;
    }
    if (t->kind == TYPE_STRUCT) {
        free(t->struct_name);
        for (int i = 0; i < t->field_count; i++) {
            free(t->field_names[i]);
            type_free(t->field_types[i]);
        }
        free(t->field_names);
        free(t->field_types);
        free(t);
        return;
    }
    if (t->kind == TYPE_MAP) {
        type_free(t->map_key_type);
    }
    type_free(t->element_type);
    free(t);
}

int type_equals(Type* a, Type* b) {
    if (a == b) return 1;
    if (a == NULL || b == NULL) return 0;
    if (a->kind != b->kind) return 0;
    if (a->kind == TYPE_ARRAY) return type_equals(a->element_type, b->element_type);
    if (a->kind == TYPE_MAP) {
        return type_equals(a->map_key_type, b->map_key_type) &&
               type_equals(a->element_type, b->element_type);
    }
    if (a->kind == TYPE_STRUCT) {
        if (a->struct_name == NULL || b->struct_name == NULL) return 0;
        return strcmp(a->struct_name, b->struct_name) == 0;
    }
    return 1;
}

int type_is_numeric(Type* t) {
    return t != NULL && t != &type_unknown &&
           (t->kind == TYPE_INT || t->kind == TYPE_FLOAT);
}
int type_is_array(Type* t) {
    return t != NULL && t != &type_unknown && t->kind == TYPE_ARRAY;
}
int type_is_map(Type* t) {
    return t != NULL && t != &type_unknown && t->kind == TYPE_MAP;
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
        case TYPE_MAP:    return "map";
        case TYPE_ROW:    return "row";
        case TYPE_CURSOR: return "cursor";
        case TYPE_STRUCT: return t->struct_name != NULL ? t->struct_name : "struct";
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
    program->structs = NULL;
    program->struct_count = 0;
    program->struct_capacity = 0;
    program->specs = NULL;
    program->spec_count = 0;
    program->spec_capacity = 0;
    program->bodies = NULL;
    program->body_count = 0;
    program->body_capacity = 0;
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
            free(expr->as.call.package_name);
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
        case EXPR_STRUCT_LITERAL:
            free(expr->as.struct_literal.struct_name);
            for (int i = 0; i < expr->as.struct_literal.field_count; i++) {
                free(expr->as.struct_literal.field_names[i]);
                free_expr(expr->as.struct_literal.values[i]);
            }
            free(expr->as.struct_literal.field_names);
            free(expr->as.struct_literal.values);
            break;
        case EXPR_MAP_LITERAL:
            for (int i = 0; i < expr->as.map_literal.count; i++) {
                free_expr(expr->as.map_literal.keys[i]);
                free_expr(expr->as.map_literal.values[i]);
            }
            free(expr->as.map_literal.keys);
            free(expr->as.map_literal.values);
            break;
        case EXPR_CURSOR_ATTR:
            free(expr->as.cursor_attr.cursor_name);
            free(expr->as.cursor_attr.attr_name);
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
    proc->is_function = 0;
    return proc;
}

void free_proc_decl(ProcDecl* proc) {
    if (proc == NULL) return;
    free(proc->name);
    free_block(proc->body);
    for (int j = 0; j < proc->param_count; j++) {
        free(proc->params[j].name);
        type_free(proc->params[j].type);
    }
    free(proc->params);
    type_free(proc->return_type);
}

void free_package_spec(PackageSpecDecl* spec) {
    if (spec == NULL) return;
    free(spec->name);
    for (int i = 0; i < spec->var_count; i++) {
        free_stmt(spec->vars[i]);
    }
    free(spec->vars);
    for (int i = 0; i < spec->proc_count; i++) {
        free_proc_decl(&spec->procs[i]);
    }
    free(spec->procs);
    for (int i = 0; i < spec->func_count; i++) {
        free_proc_decl(&spec->funcs[i]);
    }
    free(spec->funcs);
}

void free_package_body(PackageBodyDecl* body) {
    if (body == NULL) return;
    free(body->name);
    for (int i = 0; i < body->var_count; i++) {
        free_stmt(body->vars[i]);
    }
    free(body->vars);
    for (int i = 0; i < body->proc_count; i++) {
        free_proc_decl(&body->procs[i]);
    }
    free(body->procs);
    for (int i = 0; i < body->func_count; i++) {
        free_proc_decl(&body->funcs[i]);
    }
    free(body->funcs);
}

void free_program(Program* program) {
    if (program == NULL) return;
    for (int i = 0; i < program->import_count; i++) {
        free_stmt(program->imports[i]);
    }
    free(program->imports);
    for (int i = 0; i < program->struct_count; i++) {
        StructDecl* s = &program->structs[i];
        free(s->name);
        for (int j = 0; j < s->field_count; j++) {
            free(s->field_names[j]);
            type_free(s->field_types[j]);
        }
        free(s->field_names);
        free(s->field_types);
    }
    free(program->structs);
    for (int i = 0; i < program->spec_count; i++) {
        free_package_spec(&program->specs[i]);
    }
    free(program->specs);
    for (int i = 0; i < program->body_count; i++) {
        free_package_body(&program->bodies[i]);
    }
    free(program->bodies);
    for (int i = 0; i < program->proc_count; i++) {
        free_proc_decl(&program->procs[i]);
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
        case STMT_FOREACH:
            free(stmt->as.foreach_stmt.var_name);
            free_expr(stmt->as.foreach_stmt.iterable);
            free_block(stmt->as.foreach_stmt.body);
            break;
        case STMT_WHILE:
            free_expr(stmt->as.while_stmt.condition);
            free_block(stmt->as.while_stmt.body);
            break;
        case STMT_BREAK:
        case STMT_CONTINUE:
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
        case STMT_FOR_C:
            if (stmt->as.cfor_stmt.init != NULL) free_stmt(stmt->as.cfor_stmt.init);
            free_expr(stmt->as.cfor_stmt.condition);
            if (stmt->as.cfor_stmt.step != NULL) free_stmt(stmt->as.cfor_stmt.step);
            free_block(stmt->as.cfor_stmt.body);
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
        case STMT_EXCEPTION_DECL:
            free(stmt->as.exception_decl.name);
            break;
        case STMT_RAISE:
            free(stmt->as.raise_stmt.name);
            break;
        case STMT_TRY_CATCH:
            free_block(stmt->as.try_catch.try_block);
            free(stmt->as.try_catch.catch_var);
            free_block(stmt->as.try_catch.catch_block);
            break;
        case STMT_CASE:
            free_expr(stmt->as.case_stmt.selector);
            for (int i = 0; i < stmt->as.case_stmt.branch_count; i++) {
                free_expr(stmt->as.case_stmt.values[i]);
                free_block(stmt->as.case_stmt.blocks[i]);
            }
            free(stmt->as.case_stmt.values);
            free(stmt->as.case_stmt.blocks);
            free_block(stmt->as.case_stmt.else_block);
            break;
        case STMT_CURSOR_DECL:
            free(stmt->as.cursor_decl.name);
            free(stmt->as.cursor_decl.sql_query);
            break;
        case STMT_CURSOR_OPEN:
            free(stmt->as.cursor_open.name);
            free(stmt->as.cursor_open.sql_query);
            for (int i = 0; i < stmt->as.cursor_open.param_count; i++) {
                free_expr(stmt->as.cursor_open.params[i]);
            }
            free(stmt->as.cursor_open.params);
            break;
        case STMT_CURSOR_FETCH:
            free(stmt->as.cursor_fetch.name);
            for (int i = 0; i < stmt->as.cursor_fetch.into_count; i++) {
                free(stmt->as.cursor_fetch.into_vars[i]);
            }
            free(stmt->as.cursor_fetch.into_vars);
            break;
        case STMT_CURSOR_CLOSE:
            free(stmt->as.cursor_close.name);
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

Stmt* create_foreach_stmt(const char* var_name, Expr* iterable, Block* body) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (stmt == NULL) {
        free_expr(iterable);
        free_block(body);
        return NULL;
    }
    stmt->loc = (SourceLoc){0, 0};
    stmt->kind = STMT_FOREACH;
    stmt->as.foreach_stmt.var_name = copy_string(var_name);
    if (stmt->as.foreach_stmt.var_name == NULL && var_name != NULL) {
        free_expr(iterable);
        free_block(body);
        free(stmt);
        return NULL;
    }
    stmt->as.foreach_stmt.iterable = iterable;
    stmt->as.foreach_stmt.body = body;
    return stmt;
}

Stmt* create_while_stmt(Expr* condition, Block* body) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (stmt == NULL) {
        free_expr(condition);
        free_block(body);
        return NULL;
    }
    stmt->loc = (SourceLoc){0, 0};
    stmt->kind = STMT_WHILE;
    stmt->as.while_stmt.condition = condition;
    stmt->as.while_stmt.body = body;
    stmt->as.while_stmt.is_do_while = 0;
    return stmt;
}

Stmt* create_do_while_stmt(Expr* condition, Block* body) {
    Stmt* stmt = create_while_stmt(condition, body);
    if (stmt == NULL) return NULL;
    stmt->as.while_stmt.is_do_while = 1;
    return stmt;
}

Stmt* create_break_stmt(void) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (stmt == NULL) return NULL;
    stmt->loc = (SourceLoc){0, 0};
    stmt->kind = STMT_BREAK;
    return stmt;
}

Stmt* create_continue_stmt(void) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (stmt == NULL) return NULL;
    stmt->loc = (SourceLoc){0, 0};
    stmt->kind = STMT_CONTINUE;
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

Stmt* create_cfor_stmt(Stmt* init, Expr* condition, Stmt* step, Block* body) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (stmt == NULL) {
        if (init != NULL) free_stmt(init);
        free_expr(condition);
        if (step != NULL) free_stmt(step);
        free_block(body);
        return NULL;
    }
    stmt->loc = (SourceLoc){0, 0};
    stmt->kind = STMT_FOR_C;
    stmt->as.cfor_stmt.init = init;
    stmt->as.cfor_stmt.condition = condition;
    stmt->as.cfor_stmt.step = step;
    stmt->as.cfor_stmt.body = body;
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
    return create_qualified_call_expr(NULL, name, args, arg_count);
}

Expr* create_qualified_call_expr(const char* package_name, const char* name, Expr** args, int arg_count) {
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
    expr->as.call.package_name = copy_string(package_name);
    expr->as.call.name = copy_string(name);
    if ((expr->as.call.name == NULL && name != NULL) ||
        (expr->as.call.package_name == NULL && package_name != NULL)) {
        free(expr->as.call.package_name);
        free(expr->as.call.name);
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

Stmt* create_try_catch_stmt(Block* try_block, const char* catch_var, Block* catch_block) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (stmt == NULL) {
        free_block(try_block);
        free_block(catch_block);
        return NULL;
    }
    stmt->loc = (SourceLoc){0, 0};
    stmt->kind = STMT_TRY_CATCH;
    stmt->as.try_catch.try_block = try_block;
    stmt->as.try_catch.catch_var = copy_string(catch_var);
    if (stmt->as.try_catch.catch_var == NULL && catch_var != NULL) {
        free_block(try_block);
        free_block(catch_block);
        free(stmt);
        return NULL;
    }
    stmt->as.try_catch.catch_block = catch_block;
    return stmt;
}

Stmt* create_exception_decl_stmt(const char* name) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (stmt == NULL) return NULL;
    stmt->loc = (SourceLoc){0, 0};
    stmt->kind = STMT_EXCEPTION_DECL;
    stmt->as.exception_decl.name = copy_string(name);
    if (stmt->as.exception_decl.name == NULL && name != NULL) {
        free(stmt);
        return NULL;
    }
    return stmt;
}

Stmt* create_raise_stmt(const char* name) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (stmt == NULL) return NULL;
    stmt->loc = (SourceLoc){0, 0};
    stmt->kind = STMT_RAISE;
    stmt->as.raise_stmt.name = copy_string(name);
    if (stmt->as.raise_stmt.name == NULL && name != NULL) {
        free(stmt);
        return NULL;
    }
    return stmt;
}

Stmt* create_case_stmt(Expr* selector, Expr** values, Block** blocks, int branch_count, Block* else_block) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (stmt == NULL) {
        free_expr(selector);
        for (int i = 0; i < branch_count; i++) {
            free_expr(values[i]);
            free_block(blocks[i]);
        }
        free(values);
        free(blocks);
        free_block(else_block);
        return NULL;
    }
    stmt->loc = (SourceLoc){0, 0};
    stmt->kind = STMT_CASE;
    stmt->as.case_stmt.selector = selector;
    stmt->as.case_stmt.values = values;
    stmt->as.case_stmt.blocks = blocks;
    stmt->as.case_stmt.branch_count = branch_count;
    stmt->as.case_stmt.else_block = else_block;
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

Expr* create_struct_literal_expr(const char* struct_name, char** field_names, Expr** values, int field_count) {
    Expr* expr = malloc(sizeof(Expr));
    if (expr == NULL) {
        free((char*)struct_name);
        for (int i = 0; i < field_count; i++) {
            free(field_names[i]);
            free_expr(values[i]);
        }
        free(field_names);
        free(values);
        return NULL;
    }
    expr->loc = (SourceLoc){0, 0};
    expr->kind = EXPR_STRUCT_LITERAL;
    expr->as.struct_literal.struct_name = copy_string(struct_name);
    expr->as.struct_literal.field_names = field_names;
    expr->as.struct_literal.values = values;
    expr->as.struct_literal.field_count = field_count;
    return expr;
}

Expr* create_map_literal_expr(Expr** keys, Expr** values, int count) {
    Expr* expr = malloc(sizeof(Expr));
    if (expr == NULL) {
        for (int i = 0; i < count; i++) {
            free_expr(keys[i]);
            free_expr(values[i]);
        }
        free(keys);
        free(values);
        return NULL;
    }
    expr->loc = (SourceLoc){0, 0};
    expr->kind = EXPR_MAP_LITERAL;
    expr->as.map_literal.keys = keys;
    expr->as.map_literal.values = values;
    expr->as.map_literal.count = count;
    return expr;
}

Stmt* create_cursor_decl_stmt(const char* name, char* sql_query) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (stmt == NULL) {
        free(sql_query);
        return NULL;
    }
    stmt->loc = (SourceLoc){0, 0};
    stmt->kind = STMT_CURSOR_DECL;
    stmt->as.cursor_decl.name = copy_string(name);
    if (stmt->as.cursor_decl.name == NULL && name != NULL) {
        free(sql_query);
        free(stmt);
        return NULL;
    }
    stmt->as.cursor_decl.sql_query = sql_query;
    return stmt;
}

Stmt* create_cursor_open_stmt(const char* name, char* sql_query, Expr** params, int param_count) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (stmt == NULL) {
        free(sql_query);
        for (int i = 0; i < param_count; i++) free_expr(params[i]);
        free(params);
        return NULL;
    }
    stmt->loc = (SourceLoc){0, 0};
    stmt->kind = STMT_CURSOR_OPEN;
    stmt->as.cursor_open.name = copy_string(name);
    if (stmt->as.cursor_open.name == NULL && name != NULL) {
        free(sql_query);
        for (int i = 0; i < param_count; i++) free_expr(params[i]);
        free(params);
        free(stmt);
        return NULL;
    }
    stmt->as.cursor_open.sql_query = sql_query;
    stmt->as.cursor_open.params = params;
    stmt->as.cursor_open.param_count = param_count;
    return stmt;
}

Stmt* create_cursor_fetch_stmt(const char* name, char** into_vars, int into_count) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (stmt == NULL) {
        for (int i = 0; i < into_count; i++) free(into_vars[i]);
        free(into_vars);
        return NULL;
    }
    stmt->loc = (SourceLoc){0, 0};
    stmt->kind = STMT_CURSOR_FETCH;
    stmt->as.cursor_fetch.name = copy_string(name);
    if (stmt->as.cursor_fetch.name == NULL && name != NULL) {
        for (int i = 0; i < into_count; i++) free(into_vars[i]);
        free(into_vars);
        free(stmt);
        return NULL;
    }
    stmt->as.cursor_fetch.into_vars = into_vars;
    stmt->as.cursor_fetch.into_count = into_count;
    return stmt;
}

Stmt* create_cursor_close_stmt(const char* name) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (stmt == NULL) return NULL;
    stmt->loc = (SourceLoc){0, 0};
    stmt->kind = STMT_CURSOR_CLOSE;
    stmt->as.cursor_close.name = copy_string(name);
    return stmt;
}

Expr* create_cursor_attr_expr(const char* cursor_name, const char* attr_name) {
    Expr* expr = malloc(sizeof(Expr));
    if (expr == NULL) return NULL;
    expr->loc = (SourceLoc){0, 0};
    expr->kind = EXPR_CURSOR_ATTR;
    expr->as.cursor_attr.cursor_name = copy_string(cursor_name);
    if (expr->as.cursor_attr.cursor_name == NULL && cursor_name != NULL) {
        free(expr);
        return NULL;
    }
    expr->as.cursor_attr.attr_name = copy_string(attr_name);
    if (expr->as.cursor_attr.attr_name == NULL && attr_name != NULL) {
        free(expr->as.cursor_attr.cursor_name);
        free(expr);
        return NULL;
    }
    return expr;
}
