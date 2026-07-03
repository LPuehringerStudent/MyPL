#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "sql_engine.h"
#include "typecheck.h"

#define MAX_LOCALS 256
#define MAX_SCOPES 64
#define MAX_TRANSIENTS 256
#define MAX_ROWS 16

typedef struct {
    const char* name;
    Type* type;
} Local;

typedef struct {
    const char* var_name;
    const char* query;  /* pointer to AST-owned query string */
} RowBinding;

typedef struct {
    Local locals[MAX_LOCALS];
    int count;
} Scope;

typedef struct {
    ProcSignature* procs;
    int proc_count;
    Scope scopes[MAX_SCOPES];
    int scope_count;
    Type* return_type;
    struct Context* ctx;
    char* error;
    size_t error_size;
    int had_error;
    Type* transient_types[MAX_TRANSIENTS];
    int transient_count;
    RowBinding rows[MAX_ROWS];
    int row_count;
} TypeChecker;

static void type_error(TypeChecker* tc, SourceLoc loc, const char* fmt, ...) {
    if (tc->had_error) return;
    if (tc->error == NULL || tc->error_size == 0) {
        tc->had_error = 1;
        return;
    }
    va_list args;
    va_start(args, fmt);
    snprintf(tc->error, tc->error_size,
             "Type error at line %d:%d: ", loc.line, loc.column);
    size_t prefix_len = strlen(tc->error);
    vsnprintf(tc->error + prefix_len, tc->error_size - prefix_len, fmt, args);
    va_end(args);
    tc->had_error = 1;
}

static int push_scope(TypeChecker* tc) {
    if (tc->scope_count >= MAX_SCOPES) return 0;
    tc->scopes[tc->scope_count++].count = 0;
    return 1;
}

static void pop_scope(TypeChecker* tc) {
    if (tc->scope_count > 0) tc->scope_count--;
}

static int add_local(TypeChecker* tc, const char* name, Type* type) {
    Scope* scope = &tc->scopes[tc->scope_count - 1];
    if (scope->count >= MAX_LOCALS) return 0;
    scope->locals[scope->count].name = name;
    scope->locals[scope->count].type = type;
    scope->count++;
    return 1;
}

static Type* resolve_local(TypeChecker* tc, const char* name) {
    for (int s = tc->scope_count - 1; s >= 0; s--) {
        Scope* scope = &tc->scopes[s];
        for (int i = scope->count - 1; i >= 0; i--) {
            if (strcmp(scope->locals[i].name, name) == 0) {
                return scope->locals[i].type;
            }
        }
    }
    return NULL;
}

static ProcSignature* resolve_proc(TypeChecker* tc, const char* name) {
    for (int i = 0; i < tc->proc_count; i++) {
        if (strcmp(tc->procs[i].name, name) == 0) return &tc->procs[i];
    }
    return NULL;
}

static int bind_row(TypeChecker* tc, const char* var_name, const char* query) {
    if (tc->row_count >= MAX_ROWS) return 0;
    tc->rows[tc->row_count].var_name = var_name;
    tc->rows[tc->row_count].query = query;
    tc->row_count++;
    return 1;
}

static RowBinding* find_row(TypeChecker* tc, const char* var_name) {
    for (int i = tc->row_count - 1; i >= 0; i--) {
        if (strcmp(tc->rows[i].var_name, var_name) == 0) return &tc->rows[i];
    }
    return NULL;
}

static Type* sql_type_to_type(int sql_type) {
    switch (sql_type) {
        case VAL_INT:    return &type_int;
        case VAL_FLOAT:  return &type_float;
        case VAL_STRING: return &type_string;
    }
    return &type_unknown;
}

static int types_compatible(Type* expected, Type* actual) {
    if (expected == NULL || actual == NULL) return 0;
    if (type_is_unknown(expected) || type_is_unknown(actual)) return 1;
    if (type_equals(expected, actual)) return 1;
    if (type_is_numeric(expected) && type_is_numeric(actual)) return 1;
    return 0;
}

static int types_assignable(Type* target, Type* value) {
    if (target == NULL || value == NULL) return 0;
    if (type_is_unknown(target) || type_is_unknown(value)) return 1;
    if (type_equals(target, value)) return 1;
    /* Allow int -> float only. */
    if (target->kind == TYPE_FLOAT && value->kind == TYPE_INT) return 1;
    return 0;
}

static int is_condition_type(Type* t) {
    return t != NULL &&
           (type_is_unknown(t) || t->kind == TYPE_BOOL || type_is_numeric(t));
}

static int is_primitive(Type* t) {
    return t != NULL &&
           (type_is_unknown(t) || t->kind == TYPE_INT || t->kind == TYPE_FLOAT ||
            t->kind == TYPE_STRING || t->kind == TYPE_BOOL);
}

static Type* common_numeric_type(Type* left, Type* right) {
    if (left == NULL || right == NULL) return NULL;
    if (left->kind == TYPE_FLOAT || right->kind == TYPE_FLOAT) return &type_float;
    return &type_int;
}

static Type* transient_array_type(TypeChecker* tc, Type* element_type) {
    if (tc->transient_count >= MAX_TRANSIENTS) {
        type_error(tc, (SourceLoc){0,0}, "too many inferred types");
        return &type_unknown;
    }
    Type* owned_element = type_copy(element_type);
    Type* t = type_new(TYPE_ARRAY, owned_element);
    if (t == NULL) {
        type_free(owned_element);
        type_error(tc, (SourceLoc){0,0}, "out of memory");
        return &type_unknown;
    }
    tc->transient_types[tc->transient_count++] = t;
    return t;
}

static void check_stmt(TypeChecker* tc, Stmt* stmt);
static Type* infer_expr(TypeChecker* tc, Expr* expr, Type* hint);

static int is_native(const char* name) {
    return strcmp(name, "length") == 0 ||
           strcmp(name, "append") == 0 ||
           strcmp(name, "println") == 0 ||
           strcmp(name, "clock") == 0 ||
           strcmp(name, "concat") == 0 ||
           strcmp(name, "substring") == 0 ||
           strcmp(name, "contains") == 0 ||
           strcmp(name, "index_of") == 0 ||
           strcmp(name, "to_upper") == 0 ||
           strcmp(name, "to_lower") == 0 ||
           strcmp(name, "trim") == 0 ||
           strcmp(name, "int_to_string") == 0 ||
           strcmp(name, "float_to_string") == 0 ||
           strcmp(name, "abs_int") == 0 ||
           strcmp(name, "abs_float") == 0 ||
           strcmp(name, "min_int") == 0 ||
           strcmp(name, "max_int") == 0 ||
           strcmp(name, "min_float") == 0 ||
           strcmp(name, "max_float") == 0 ||
           strcmp(name, "read_file") == 0 ||
           strcmp(name, "write_file") == 0 ||
           strcmp(name, "file_exists") == 0 ||
           strcmp(name, "split") == 0 ||
           strcmp(name, "join") == 0 ||
           strcmp(name, "replace") == 0 ||
           strcmp(name, "repeat") == 0;
}

static Type* check_native_call(TypeChecker* tc, const char* name, Expr** args, int arg_count, SourceLoc loc) {
    if (strcmp(name, "length") == 0) {
        if (arg_count != 1) {
            type_error(tc, loc, "length expects 1 argument");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        if (a != &type_unknown && a != NULL &&
            a->kind != TYPE_STRING && !type_is_array(a)) {
            type_error(tc, loc, "length expects a string or array");
        }
        return &type_int;
    }
    if (strcmp(name, "append") == 0) {
        if (arg_count != 2) {
            type_error(tc, loc, "append expects 2 arguments");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        if (!type_is_array(a) && a != &type_unknown) {
            type_error(tc, loc, "append expects an array");
            return a;
        }
        Type* v = infer_expr(tc, args[1], a != NULL && type_is_array(a) ? a->element_type : NULL);
        if (a->element_type != NULL && v != NULL && !type_equals(a->element_type, v) && v != &type_unknown) {
            type_error(tc, loc, "append value type does not match array element type");
        }
        return a;
    }
    if (strcmp(name, "println") == 0) {
        if (arg_count != 1) {
            type_error(tc, loc, "println expects 1 argument");
            return NULL;
        }
        (void)infer_expr(tc, args[0], NULL);
        return &type_int;
    }
    if (strcmp(name, "clock") == 0) {
        if (arg_count != 0) {
            type_error(tc, loc, "clock expects 0 arguments");
            return NULL;
        }
        return &type_int;
    }
    if (strcmp(name, "concat") == 0) {
        if (arg_count != 2) {
            type_error(tc, loc, "concat expects 2 arguments");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        Type* b = infer_expr(tc, args[1], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_STRING) {
            type_error(tc, loc, "concat expects string arguments");
        }
        if (b != &type_unknown && b != NULL && b->kind != TYPE_STRING) {
            type_error(tc, loc, "concat expects string arguments");
        }
        return &type_string;
    }
    if (strcmp(name, "substring") == 0) {
        if (arg_count != 3) {
            type_error(tc, loc, "substring expects 3 arguments");
            return NULL;
        }
        Type* s = infer_expr(tc, args[0], NULL);
        Type* start = infer_expr(tc, args[1], NULL);
        Type* len = infer_expr(tc, args[2], NULL);
        if (s != &type_unknown && s != NULL && s->kind != TYPE_STRING) {
            type_error(tc, loc, "substring expects a string as first argument");
        }
        if (start != &type_unknown && start != NULL && start->kind != TYPE_INT) {
            type_error(tc, loc, "substring expects int start");
        }
        if (len != &type_unknown && len != NULL && len->kind != TYPE_INT) {
            type_error(tc, loc, "substring expects int length");
        }
        return &type_string;
    }
    if (strcmp(name, "contains") == 0 || strcmp(name, "index_of") == 0) {
        if (arg_count != 2) {
            type_error(tc, loc, "%s expects 2 arguments", name);
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        Type* b = infer_expr(tc, args[1], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_STRING) {
            type_error(tc, loc, "%s expects string arguments", name);
        }
        if (b != &type_unknown && b != NULL && b->kind != TYPE_STRING) {
            type_error(tc, loc, "%s expects string arguments", name);
        }
        return strcmp(name, "contains") == 0 ? &type_bool : &type_int;
    }
    if (strcmp(name, "to_upper") == 0 ||
        strcmp(name, "to_lower") == 0 ||
        strcmp(name, "trim") == 0) {
        if (arg_count != 1) {
            type_error(tc, loc, "%s expects 1 argument", name);
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_STRING) {
            type_error(tc, loc, "%s expects a string", name);
        }
        return &type_string;
    }
    if (strcmp(name, "int_to_string") == 0) {
        if (arg_count != 1) {
            type_error(tc, loc, "int_to_string expects 1 argument");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_INT) {
            type_error(tc, loc, "int_to_string expects an int");
        }
        return &type_string;
    }
    if (strcmp(name, "float_to_string") == 0) {
        if (arg_count != 1) {
            type_error(tc, loc, "float_to_string expects 1 argument");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_FLOAT) {
            type_error(tc, loc, "float_to_string expects a float");
        }
        return &type_string;
    }
    if (strcmp(name, "abs_int") == 0) {
        if (arg_count != 1) {
            type_error(tc, loc, "abs_int expects 1 argument");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_INT) {
            type_error(tc, loc, "abs_int expects an int");
        }
        return &type_int;
    }
    if (strcmp(name, "abs_float") == 0) {
        if (arg_count != 1) {
            type_error(tc, loc, "abs_float expects 1 argument");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_FLOAT) {
            type_error(tc, loc, "abs_float expects a float");
        }
        return &type_float;
    }
    if (strcmp(name, "min_int") == 0 || strcmp(name, "max_int") == 0) {
        if (arg_count != 2) {
            type_error(tc, loc, "%s expects 2 arguments", name);
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        Type* b = infer_expr(tc, args[1], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_INT) {
            type_error(tc, loc, "%s expects int arguments", name);
        }
        if (b != &type_unknown && b != NULL && b->kind != TYPE_INT) {
            type_error(tc, loc, "%s expects int arguments", name);
        }
        return &type_int;
    }
    if (strcmp(name, "min_float") == 0 || strcmp(name, "max_float") == 0) {
        if (arg_count != 2) {
            type_error(tc, loc, "%s expects 2 arguments", name);
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        Type* b = infer_expr(tc, args[1], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_FLOAT) {
            type_error(tc, loc, "%s expects float arguments", name);
        }
        if (b != &type_unknown && b != NULL && b->kind != TYPE_FLOAT) {
            type_error(tc, loc, "%s expects float arguments", name);
        }
        return &type_float;
    }
    if (strcmp(name, "read_file") == 0) {
        if (arg_count != 1) {
            type_error(tc, loc, "read_file expects 1 argument");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_STRING) {
            type_error(tc, loc, "read_file expects a string path");
        }
        return &type_string;
    }
    if (strcmp(name, "write_file") == 0) {
        if (arg_count != 2) {
            type_error(tc, loc, "write_file expects 2 arguments");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        Type* b = infer_expr(tc, args[1], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_STRING) {
            type_error(tc, loc, "write_file expects a string path");
        }
        if (b != &type_unknown && b != NULL && b->kind != TYPE_STRING) {
            type_error(tc, loc, "write_file expects string contents");
        }
        return &type_int;
    }
    if (strcmp(name, "file_exists") == 0) {
        if (arg_count != 1) {
            type_error(tc, loc, "file_exists expects 1 argument");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_STRING) {
            type_error(tc, loc, "file_exists expects a string path");
        }
        return &type_bool;
    }
    if (strcmp(name, "split") == 0) {
        if (arg_count != 2) {
            type_error(tc, loc, "split expects 2 arguments");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        Type* b = infer_expr(tc, args[1], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_STRING) {
            type_error(tc, loc, "split expects string arguments");
        }
        if (b != &type_unknown && b != NULL && b->kind != TYPE_STRING) {
            type_error(tc, loc, "split expects string arguments");
        }
        return transient_array_type(tc, &type_string);
    }
    if (strcmp(name, "join") == 0) {
        if (arg_count != 2) {
            type_error(tc, loc, "join expects 2 arguments");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], transient_array_type(tc, &type_string));
        Type* b = infer_expr(tc, args[1], NULL);
        if (a != &type_unknown && a != NULL && !type_is_array(a)) {
            type_error(tc, loc, "join expects an array<string>");
        }
        if (a != NULL && type_is_array(a) &&
            a->element_type != NULL &&
            a->element_type->kind != TYPE_UNKNOWN &&
            a->element_type->kind != TYPE_STRING) {
            type_error(tc, loc, "join expects an array<string>");
        }
        if (b != &type_unknown && b != NULL && b->kind != TYPE_STRING) {
            type_error(tc, loc, "join expects a string delimiter");
        }
        return &type_string;
    }
    if (strcmp(name, "replace") == 0) {
        if (arg_count != 3) {
            type_error(tc, loc, "replace expects 3 arguments");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        Type* b = infer_expr(tc, args[1], NULL);
        Type* c = infer_expr(tc, args[2], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_STRING) {
            type_error(tc, loc, "replace expects string arguments");
        }
        if (b != &type_unknown && b != NULL && b->kind != TYPE_STRING) {
            type_error(tc, loc, "replace expects string arguments");
        }
        if (c != &type_unknown && c != NULL && c->kind != TYPE_STRING) {
            type_error(tc, loc, "replace expects string arguments");
        }
        return &type_string;
    }
    if (strcmp(name, "repeat") == 0) {
        if (arg_count != 2) {
            type_error(tc, loc, "repeat expects 2 arguments");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        Type* b = infer_expr(tc, args[1], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_STRING) {
            type_error(tc, loc, "repeat expects a string");
        }
        if (b != &type_unknown && b != NULL && b->kind != TYPE_INT) {
            type_error(tc, loc, "repeat expects an int count");
        }
        return &type_string;
    }
    return NULL;
}

static void check_block(TypeChecker* tc, Block* block) {
    if (!push_scope(tc)) {
        type_error(tc, (SourceLoc){0, 0}, "too many nested scopes");
        return;
    }
    for (int i = 0; i < block->stmt_count; i++) {
        check_stmt(tc, block->stmts[i]);
        if (tc->had_error) {
            pop_scope(tc);
            return;
        }
    }
    pop_scope(tc);
}

static Type* infer_expr(TypeChecker* tc, Expr* expr, Type* hint) {
    switch (expr->kind) {
        case EXPR_LITERAL: {
            Value v = expr->as.literal.value;
            switch (v.type) {
                case VAL_INT:    return &type_int;
                case VAL_FLOAT:  return &type_float;
                case VAL_STRING: return &type_string;
                case VAL_BOOL:   return &type_bool;
                case VAL_ARRAY:  return &type_int; /* unreachable for literals */
            }
            return NULL;
        }

        case EXPR_VARIABLE: {
            Type* t = resolve_local(tc, expr->as.variable.name);
            if (t == NULL) {
                type_error(tc, expr->loc, "Undefined variable '%s'",
                           expr->as.variable.name);
            }
            return t;
        }

        case EXPR_BINARY: {
            Type* left  = infer_expr(tc, expr->as.binary.left, NULL);
            Type* right = infer_expr(tc, expr->as.binary.right, NULL);
            TokenType op = expr->as.binary.op;

            if (tc->had_error) return NULL;

            if (op == TOKEN_PLUS || op == TOKEN_MINUS ||
                op == TOKEN_STAR || op == TOKEN_SLASH) {
                if (op == TOKEN_PLUS &&
                    left != NULL && left->kind == TYPE_STRING &&
                    right != NULL && right->kind == TYPE_STRING) {
                    return &type_string;
                }
                if ((left != &type_unknown && !type_is_numeric(left)) ||
                    (right != &type_unknown && !type_is_numeric(right))) {
                    type_error(tc, expr->loc,
                               "Operands to '%s' must be numeric",
                               op == TOKEN_PLUS ? "+" :
                               op == TOKEN_MINUS ? "-" :
                               op == TOKEN_STAR ? "*" : "/");
                    return NULL;
                }
                return common_numeric_type(left, right);
            }

            if (op == TOKEN_EQ || op == TOKEN_NE ||
                op == TOKEN_LT || op == TOKEN_GT ||
                op == TOKEN_GE || op == TOKEN_LE) {
                if (!is_primitive(left) || !is_primitive(right) ||
                    !types_compatible(left, right)) {
                    type_error(tc, expr->loc,
                               "Incompatible operand types for comparison");
                    return NULL;
                }
                return &type_bool;
            }

            type_error(tc, expr->loc, "Unknown binary operator");
            return NULL;
        }

        case EXPR_UNARY: {
            Type* operand = infer_expr(tc, expr->as.unary.operand, NULL);
            TokenType op = expr->as.unary.op;

            if (tc->had_error) return NULL;

            if (op == TOKEN_MINUS) {
                if (operand != &type_unknown && !type_is_numeric(operand)) {
                    type_error(tc, expr->loc,
                               "Operand to '-' must be numeric");
                    return NULL;
                }
                return operand == &type_unknown ? &type_int : operand;
            }

            if (op == TOKEN_BANG) {
                if (operand != NULL && operand != &type_unknown &&
                    operand->kind != TYPE_BOOL && !type_is_numeric(operand)) {
                    type_error(tc, expr->loc,
                               "Operand to '!' must be bool or numeric");
                    return NULL;
                }
                return &type_bool;
            }

            type_error(tc, expr->loc, "Unknown unary operator");
            return NULL;
        }

        case EXPR_CALL: {
            const char* call_name = expr->as.call.name;
            if (is_native(call_name)) {
                return check_native_call(tc, call_name, expr->as.call.args,
                                         expr->as.call.arg_count, expr->loc);
            }
            ProcSignature* sig = resolve_proc(tc, call_name);
            if (sig == NULL) {
                type_error(tc, expr->loc, "Undefined procedure '%s'",
                           call_name);
                return NULL;
            }
            if (expr->as.call.arg_count != sig->param_count) {
                type_error(tc, expr->loc,
                           "Procedure '%s' expects %d arguments but got %d",
                           expr->as.call.name, sig->param_count,
                           expr->as.call.arg_count);
                return sig->return_type;
            }
            for (int i = 0; i < expr->as.call.arg_count; i++) {
                Type* arg_type = infer_expr(tc, expr->as.call.args[i],
                                            sig->param_types[i]);
                if (tc->had_error) return sig->return_type;
                if (!types_assignable(sig->param_types[i], arg_type)) {
                    type_error(tc, expr->as.call.args[i]->loc,
                               "Argument %d of '%s' has incompatible type",
                               i + 1, expr->as.call.name);
                    return sig->return_type;
                }
            }
            return sig->return_type;
        }

        case EXPR_ARRAY: {
            int count = expr->as.array.count;
            if (count == 0) {
                if (hint != NULL && hint->kind == TYPE_ARRAY) {
                    return transient_array_type(tc, hint->element_type);
                }
                type_error(tc, expr->loc,
                           "Cannot infer element type of empty array");
                return transient_array_type(tc, &type_int);
            }
            Type* elem_type = infer_expr(tc, expr->as.array.elements[0], NULL);
            for (int i = 1; i < count; i++) {
                Type* t = infer_expr(tc, expr->as.array.elements[i], NULL);
                if (tc->had_error) return NULL;
                if (!type_equals(elem_type, t)) {
                    type_error(tc, expr->as.array.elements[i]->loc,
                               "Array element type mismatch");
                    return NULL;
                }
            }
            return transient_array_type(tc, elem_type);
        }

        case EXPR_INDEX: {
            Type* base = infer_expr(tc, expr->as.index.array, NULL);
            Type* idx  = infer_expr(tc, expr->as.index.index, NULL);
            if (tc->had_error) return NULL;
            if (base == NULL || base->kind != TYPE_ARRAY) {
                type_error(tc, expr->loc, "Cannot index non-array type");
                return NULL;
            }
            if (idx == NULL || idx->kind != TYPE_INT) {
                type_error(tc, expr->loc, "Array index must be int");
                return NULL;
            }
            return base->element_type;
        }

        case EXPR_FIELD: {
            Type* base = resolve_local(tc, expr->as.field.row);
            if (base == NULL) {
                type_error(tc, expr->loc, "Undefined variable '%s'",
                           expr->as.field.row);
                return &type_unknown;
            }
            if (base != &type_unknown) {
                type_error(tc, expr->loc,
                           "Cannot access field on non-row variable '%s'",
                           expr->as.field.row);
                return &type_unknown;
            }
            RowBinding* row = find_row(tc, expr->as.field.row);
            if (row != NULL && tc->ctx != NULL) {
                int sql_type;
                if (sql_query_column_type(tc->ctx, row->query, expr->as.field.field, &sql_type)) {
                    return sql_type_to_type(sql_type);
                }
                type_error(tc, expr->loc,
                           "Unknown column '%s' for row variable '%s'",
                           expr->as.field.field, expr->as.field.row);
                return &type_unknown;
            }
            return &type_unknown;
        }

        case EXPR_SQL_PARAM: {
            Type* t = resolve_local(tc, expr->as.sql_param.name);
            if (t == NULL) {
                type_error(tc, expr->loc, "Undefined variable '%s'",
                           expr->as.sql_param.name);
                return &type_unknown;
            }
            return t;
        }

        case EXPR_ROW_FIELD: {
            Type* base = infer_expr(tc, expr->as.row_field.row, NULL);
            if (tc->had_error) return NULL;
            if (base == NULL || base->kind != TYPE_ROW) {
                type_error(tc, expr->loc, "Cannot access field on non-row expression");
                return &type_unknown;
            }
            return &type_unknown;
        }
    }

    return NULL;
}

static void check_stmt(TypeChecker* tc, Stmt* stmt) {
    switch (stmt->kind) {
        case STMT_VAR_DECL: {
            Type* declared = stmt->as.var_decl.type;
            Type* final_type = declared;

            if (stmt->as.var_decl.initializer != NULL) {
                Type* init_type = infer_expr(tc, stmt->as.var_decl.initializer, declared);
                if (tc->had_error) return;
                if (init_type == NULL) {
                    type_error(tc, stmt->loc, "Cannot infer type of initializer");
                    return;
                }

                if (declared != NULL && declared->kind == TYPE_ARRAY &&
                    declared->element_type == NULL &&
                    init_type->kind == TYPE_ARRAY) {
                    final_type = init_type;
                } else if (!types_assignable(declared, init_type)) {
                    type_error(tc, stmt->loc,
                               "Cannot assign value of type '%s' to variable of type '%s'",
                               type_name(init_type), type_name(declared));
                    return;
                }
            }

            if (!add_local(tc, stmt->as.var_decl.name, final_type)) {
                type_error(tc, stmt->loc, "Too many local variables");
            }
            break;
        }

        case STMT_ASSIGN: {
            Type* target = resolve_local(tc, stmt->as.assign.name);
            if (target == NULL) {
                type_error(tc, stmt->loc, "Undefined variable '%s'",
                           stmt->as.assign.name);
                return;
            }
            Type* rhs = infer_expr(tc, stmt->as.assign.value, target);
            if (tc->had_error) return;
            if (!types_assignable(target, rhs)) {
                type_error(tc, stmt->loc,
                           "Cannot assign value of type '%s' to variable of type '%s'",
                           type_name(rhs), type_name(target));
            }
            break;
        }

        case STMT_RETURN: {
            Type* value = infer_expr(tc, stmt->as.return_stmt.value, tc->return_type);
            if (tc->had_error) return;
            if (!types_assignable(tc->return_type, value)) {
                type_error(tc, stmt->loc,
                           "Return value of type '%s' incompatible with return type '%s'",
                           type_name(value), type_name(tc->return_type));
            }
            break;
        }

        case STMT_IF: {
            Type* cond = infer_expr(tc, stmt->as.if_stmt.condition, NULL);
            if (tc->had_error) return;
            if (!is_condition_type(cond)) {
                type_error(tc, stmt->loc,
                           "If condition must be bool or numeric");
                return;
            }
            check_block(tc, stmt->as.if_stmt.then_block);
            if (tc->had_error) return;
            if (stmt->as.if_stmt.else_block != NULL) {
                check_block(tc, stmt->as.if_stmt.else_block);
            }
            break;
        }

        case STMT_FOR: {
            if (!push_scope(tc)) {
                type_error(tc, stmt->loc, "Too many nested scopes");
                return;
            }
            ForStmt* f = &stmt->as.for_stmt;
            for (int i = 0; i < f->param_count; i++) {
                infer_expr(tc, f->params[i], NULL);
                if (tc->had_error) {
                    pop_scope(tc);
                    return;
                }
            }
            int saved_row_count = tc->row_count;
            if (tc->ctx != NULL) {
                if (!bind_row(tc, f->var_name, f->sql_query)) {
                    type_error(tc, stmt->loc, "too many row bindings");
                    tc->row_count = saved_row_count;
                    pop_scope(tc);
                    return;
                }
            }
            if (!add_local(tc, f->var_name, &type_unknown)) {
                type_error(tc, stmt->loc, "Too many local variables");
                tc->row_count = saved_row_count;
                pop_scope(tc);
                return;
            }
            check_block(tc, f->body);
            tc->row_count = saved_row_count;
            pop_scope(tc);
            break;
        }

        case STMT_PRINT:
        case STMT_EXPR: {
            Expr* e = (stmt->kind == STMT_PRINT)
                          ? stmt->as.print_stmt.value
                          : stmt->as.expr_stmt.value;
            infer_expr(tc, e, NULL);
            break;
        }

        case STMT_INDEX_ASSIGN: {
            Type* base = infer_expr(tc, stmt->as.index_assign.array, NULL);
            Type* idx  = infer_expr(tc, stmt->as.index_assign.index, NULL);
            Type* val  = infer_expr(tc, stmt->as.index_assign.value,
                                    base != NULL && base->kind == TYPE_ARRAY
                                        ? base->element_type : NULL);
            if (tc->had_error) return;
            if (base == NULL || base->kind != TYPE_ARRAY) {
                type_error(tc, stmt->loc, "Cannot index assign to non-array type");
                return;
            }
            if (idx == NULL || idx->kind != TYPE_INT) {
                type_error(tc, stmt->loc, "Array index must be int");
                return;
            }
            if (!types_assignable(base->element_type, val)) {
                type_error(tc, stmt->loc,
                           "Cannot assign value of type '%s' to array element of type '%s'",
                           type_name(val), type_name(base->element_type));
            }
            break;
        }

        case STMT_IMPORT: {
            /* imports have no type checking */
            break;
        }

        case STMT_SQL_DDL:
        case STMT_SQL_DML: {
            SqlStmt* s = &stmt->as.sql_stmt;
            for (int i = 0; i < s->param_count; i++) {
                infer_expr(tc, s->params[i], NULL);
                if (tc->had_error) return;
            }
            break;
        }
        case STMT_SQL_QUERY: {
            SqlStmt* s = &stmt->as.sql_stmt;
            for (int i = 0; i < s->param_count; i++) {
                infer_expr(tc, s->params[i], NULL);
                if (tc->had_error) return;
            }
            if (s->into_count == 0) {
                type_error(tc, stmt->loc, "SELECT statement requires INTO");
                return;
            }
            int into_array = 0;
            if (s->into_count == 1) {
                Type* t = resolve_local(tc, s->into_vars[0]);
                if (t != NULL && t->kind == TYPE_ARRAY && t->element_type != NULL && t->element_type->kind == TYPE_ROW) {
                    into_array = 1;
                }
            }
            for (int i = 0; i < s->into_count; i++) {
                Type* t = resolve_local(tc, s->into_vars[i]);
                if (t == NULL) {
                    type_error(tc, stmt->loc, "Undefined variable '%s'", s->into_vars[i]);
                    return;
                }
                if (into_array) continue;
                if (t->kind != TYPE_INT && t->kind != TYPE_FLOAT && t->kind != TYPE_STRING && t->kind != TYPE_BOOL) {
                    type_error(tc, stmt->loc, "SELECT INTO target must be a scalar variable");
                    return;
                }
            }
            break;
        }

        case STMT_SQL_TRANSACTION: {
            /* no type checking needed */
            break;
        }
    }
}

int typecheck_program(Program* program,
                      ProcSignature* procs,
                      int proc_count,
                      struct Context* ctx,
                      char* error,
                      size_t error_size) {
    if (error != NULL && error_size > 0) {
        error[0] = '\0';
    }

    if (program == NULL) {
        if (error != NULL && error_size > 0) {
            snprintf(error, error_size, "Type error: program is NULL");
        }
        return 0;
    }

    TypeChecker tc = {0};
    tc.ctx = ctx;
    tc.error = error;
    tc.error_size = error_size;

    if (procs == NULL && proc_count > 0) {
        proc_count = 0;
    }

    ProcSignature* combined_procs = NULL;
    int combined_count = program->proc_count + proc_count;
    if (combined_count > 0) {
        combined_procs = malloc(sizeof(ProcSignature) * combined_count);
        if (combined_procs == NULL) {
            if (error != NULL && error_size > 0) {
                snprintf(error, error_size, "Type error: out of memory");
            }
            return 0;
        }
        for (int i = 0; i < program->proc_count; i++) {
            ProcDecl* proc = &program->procs[i];
            Type** param_types = malloc(sizeof(Type*) * proc->param_count);
            if (param_types == NULL && proc->param_count > 0) {
                for (int j = 0; j < i; j++) {
                    free(combined_procs[j].param_types);
                }
                free(combined_procs);
                if (error != NULL && error_size > 0) {
                    snprintf(error, error_size, "Type error: out of memory");
                }
                return 0;
            }
            for (int p = 0; p < proc->param_count; p++) {
                param_types[p] = proc->params[p].type;
            }
            combined_procs[i].name = proc->name;
            combined_procs[i].return_type = proc->return_type;
            combined_procs[i].param_types = param_types;
            combined_procs[i].param_count = proc->param_count;
        }
        for (int i = 0; i < proc_count; i++) {
            combined_procs[program->proc_count + i] = procs[i];
            if (procs[i].param_count > 0 && procs[i].param_types == NULL) {
                combined_procs[program->proc_count + i].param_count = 0;
                combined_procs[program->proc_count + i].param_types = NULL;
            }
        }
    }
    tc.procs = combined_procs;
    tc.proc_count = combined_count;

    for (int i = 0; i < program->proc_count; i++) {
        ProcDecl* proc = &program->procs[i];
        if (!push_scope(&tc)) {
            type_error(&tc, (SourceLoc){0, 0}, "Too many nested scopes");
            break;
        }
        for (int p = 0; p < proc->param_count; p++) {
            if (!add_local(&tc, proc->params[p].name, proc->params[p].type)) {
                type_error(&tc, (SourceLoc){0, 0}, "Too many local variables");
                break;
            }
        }
        if (tc.had_error) {
            pop_scope(&tc);
            break;
        }
        tc.return_type = proc->return_type;
        check_block(&tc, proc->body);
        pop_scope(&tc);
        if (tc.had_error) break;
    }

    for (int i = 0; i < tc.transient_count; i++) {
        type_free(tc.transient_types[i]);
    }

    for (int i = 0; i < program->proc_count; i++) {
        free(combined_procs[i].param_types);
    }
    free(combined_procs);

    return !tc.had_error;
}
