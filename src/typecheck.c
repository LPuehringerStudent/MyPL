#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "diagnostics.h"
#include "sql_engine.h"
#include "typecheck.h"

#define MAX_LOCALS 256
#define MAX_SCOPES 64
#define MAX_TRANSIENTS 256
#define MAX_ROWS 16
#define MAX_STRUCTS 64
#define MAX_SUBTYPES 64
#define MAX_EXCEPTIONS 64

typedef struct {
    const char* name;
    Type* type;
} Local;

typedef struct {
    char* name;
    char** field_names;
    Type** field_types;
    int field_count;
} StructInfo;

typedef struct {
    const char* var_name;
    const char* query;  /* pointer to AST-owned query string */
} RowBinding;

typedef struct {
    const char* var_name;
    const char* query;  /* pointer to AST-owned query string */
} CursorBinding;

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
    const char* source_path;
    char* error;
    size_t error_size;
    int had_error;
    Type* transient_types[MAX_TRANSIENTS];
    int transient_count;
    RowBinding rows[MAX_ROWS];
    int row_count;
    CursorBinding cursors[MAX_ROWS];
    int cursor_count;
    StructInfo structs[MAX_STRUCTS];
    int struct_count;
    struct { const char* name; Type* base; } subtypes[MAX_SUBTYPES];
    int subtype_count;
    const char* current_package;
    Program* program;
    const char* exceptions[MAX_EXCEPTIONS];
    int exception_count;
} TypeChecker;

static void type_error(TypeChecker* tc, SourceLoc loc, const char* fmt, ...) {
    if (tc->had_error) return;
    if (tc->error == NULL || tc->error_size == 0) {
        tc->had_error = 1;
        return;
    }
    char msg[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    format_error(tc->error, tc->error_size, tc->source_path,
                 loc.line, loc.column, msg);
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

static int resolve_exception(TypeChecker* tc, const char* name) {
    for (int i = 0; i < tc->exception_count; i++) {
        if (strcmp(tc->exceptions[i], name) == 0) return 1;
    }
    return 0;
}

static int add_exception(TypeChecker* tc, const char* name) {
    if (tc->exception_count >= MAX_EXCEPTIONS) return 0;
    if (resolve_exception(tc, name)) return 1;  /* idempotent */
    tc->exceptions[tc->exception_count++] = name;
    return 1;
}

static ProcSignature* resolve_proc(TypeChecker* tc, const char* name) {
    for (int i = 0; i < tc->proc_count; i++) {
        if (strcmp(tc->procs[i].name, name) == 0) return &tc->procs[i];
    }
    return NULL;
}

static ProcSignature* resolve_proc_in_package(TypeChecker* tc, const char* package_name, const char* name) {
    size_t len = strlen(package_name) + 1 + strlen(name) + 1;
    char* mangled = malloc(len);
    if (mangled == NULL) return NULL;
    snprintf(mangled, len, "%s.%s", package_name, name);
    ProcSignature* sig = resolve_proc(tc, mangled);
    free(mangled);
    return sig;
}

static int package_has_public_member(Program* program, const char* package_name, const char* member_name) {
    for (int i = 0; i < program->spec_count; i++) {
        PackageSpecDecl* spec = &program->specs[i];
        if (strcmp(spec->name, package_name) != 0) continue;
        for (int p = 0; p < spec->proc_count; p++) {
            if (strcmp(spec->procs[p].name, member_name) == 0) return 1;
        }
        for (int p = 0; p < spec->func_count; p++) {
            if (strcmp(spec->funcs[p].name, member_name) == 0) return 1;
        }
    }
    return 0;
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

static int bind_cursor(TypeChecker* tc, const char* var_name, const char* query) {
    if (tc->cursor_count >= MAX_ROWS) return 0;
    tc->cursors[tc->cursor_count].var_name = var_name;
    tc->cursors[tc->cursor_count].query = query;
    tc->cursor_count++;
    return 1;
}

static StructInfo* find_struct(TypeChecker* tc, const char* name) {
    for (int i = 0; i < tc->struct_count; i++) {
        if (strcmp(tc->structs[i].name, name) == 0) return &tc->structs[i];
    }
    return NULL;
}

static Type* struct_field_type(TypeChecker* tc, const char* struct_name, const char* field_name);
static Type* transient_array_type(TypeChecker* tc, Type* element_type);

static Type* struct_field_type(TypeChecker* tc, const char* struct_name, const char* field_name) {
    StructInfo* info = find_struct(tc, struct_name);
    if (info == NULL) return NULL;
    for (int i = 0; i < info->field_count; i++) {
        if (strcmp(info->field_names[i], field_name) == 0) {
            return info->field_types[i];
        }
    }
    return NULL;
}

static Type* make_struct_type(TypeChecker* tc, const char* name) {
    Type* t = transient_array_type(tc, NULL);
    if (t == NULL) return NULL;
    /* transient_array_type returns TYPE_ARRAY; repurpose the allocation. */
    t->kind = TYPE_STRUCT;
    t->element_type = NULL;
    t->struct_name = strdup(name);
    t->field_names = NULL;
    t->field_types = NULL;
    t->field_count = 0;
    return t;
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

static Type* transient_map_type(TypeChecker* tc, Type* key_type, Type* value_type) {
    if (tc->transient_count >= MAX_TRANSIENTS) {
        type_error(tc, (SourceLoc){0,0}, "too many inferred types");
        return &type_unknown;
    }
    Type* owned_key = type_copy(key_type);
    Type* owned_value = type_copy(value_type);
    Type* t = type_new_map(owned_key, owned_value);
    if (t == NULL) {
        type_free(owned_key);
        type_free(owned_value);
        type_error(tc, (SourceLoc){0,0}, "out of memory");
        return &type_unknown;
    }
    tc->transient_types[tc->transient_count++] = t;
    return t;
}

static Type* transient_struct_type(TypeChecker* tc, const char* name, char** field_names, Type** field_types, int field_count) {
    if (tc->transient_count >= MAX_TRANSIENTS) {
        type_error(tc, (SourceLoc){0,0}, "too many inferred types");
        return &type_unknown;
    }
    Type* t = type_new(TYPE_STRUCT, NULL);
    if (t == NULL) {
        type_error(tc, (SourceLoc){0,0}, "out of memory");
        return &type_unknown;
    }
    t->struct_name = malloc(strlen(name) + 1);
    if (t->struct_name == NULL) {
        free(t);
        type_error(tc, (SourceLoc){0,0}, "out of memory");
        return &type_unknown;
    }
    strcpy(t->struct_name, name);
    t->field_count = field_count;
    if (field_count > 0) {
        t->field_names = malloc(sizeof(char*) * (size_t)field_count);
        t->field_types = malloc(sizeof(Type*) * (size_t)field_count);
        if (t->field_names == NULL || t->field_types == NULL) {
            free(t->struct_name);
            free(t->field_names);
            free(t->field_types);
            free(t);
            type_error(tc, (SourceLoc){0,0}, "out of memory");
            return &type_unknown;
        }
        for (int i = 0; i < field_count; i++) {
            t->field_names[i] = malloc(strlen(field_names[i]) + 1);
            if (t->field_names[i] != NULL) strcpy(t->field_names[i], field_names[i]);
            t->field_types[i] = field_types[i];
        }
    }
    tc->transient_types[tc->transient_count++] = t;
    return t;
}

static Type* resolve_percent_type(TypeChecker* tc, Type* t, SourceLoc loc) {
    if (t == NULL) return &type_unknown;
    if (t->kind == TYPE_PERCENT_TYPE) {
        if (t->field_count > 0 && t->field_names != NULL) {
            /* table.column%type */
            if (tc->ctx == NULL) {
                type_error(tc, loc, "Cannot resolve column type without a database context");
                return &type_unknown;
            }
            const char* table_name = t->struct_name;
            const char* column_name = t->field_names[0];
            char query[512];
            snprintf(query, sizeof(query), "select %s from %s", column_name, table_name);
            int sql_type;
            if (sql_query_column_type(tc->ctx, query, column_name, &sql_type)) {
                return sql_type_to_type(sql_type);
            }
            type_error(tc, loc, "Could not resolve type of %s.%s", table_name, column_name);
            return &type_unknown;
        }
        /* var%type */
        const char* var_name = t->struct_name;
        Type* resolved = resolve_local(tc, var_name);
        if (resolved == NULL) {
            type_error(tc, loc, "Undefined variable '%s' in %TYPE", var_name);
            return &type_unknown;
        }
        return resolved;
    }
    if (t->kind == TYPE_PERCENT_ROWTYPE) {
        if (tc->ctx == NULL) {
            type_error(tc, loc, "Cannot resolve row type without a database context");
            return &type_unknown;
        }
        const char* table_name = t->struct_name;
        Table* table = catalog_find_table(tc->ctx, table_name);
        if (table == NULL) {
            type_error(tc, loc, "Unknown table '%s' in %ROWTYPE", table_name);
            return &type_unknown;
        }
        char** names = malloc(sizeof(char*) * (size_t)table->column_count);
        Type** types = malloc(sizeof(Type*) * (size_t)table->column_count);
        if (names == NULL || types == NULL) {
            free(names);
            free(types);
            type_error(tc, loc, "out of memory");
            return &type_unknown;
        }
        for (int i = 0; i < table->column_count; i++) {
            names[i] = table->columns[i].name;
            types[i] = sql_type_to_type(table->columns[i].type);
        }
        Type* resolved = transient_struct_type(tc, table_name, names, types, table->column_count);
        free(names);
        free(types);
        return resolved;
    }
    return t;
}

static Type* resolve_field_type(TypeChecker* tc, Type* base, const char* field_name,
                                SourceLoc loc) {
    if (base == NULL || field_name == NULL) return NULL;
    if (base->kind == TYPE_PERCENT_ROWTYPE) {
        if (tc->ctx == NULL) {
            type_error(tc, loc, "Cannot resolve row type without a database context");
            return NULL;
        }
        const char* table_name = base->struct_name;
        Table* table = catalog_find_table(tc->ctx, table_name);
        if (table == NULL) {
            type_error(tc, loc, "Unknown table '%s' in %ROWTYPE", table_name);
            return NULL;
        }
        for (int i = 0; i < table->column_count; i++) {
            if (strcmp(table->columns[i].name, field_name) == 0) {
                return sql_type_to_type(table->columns[i].type);
            }
        }
        type_error(tc, loc, "Unknown column '%s' in table '%s'", field_name, table_name);
        return NULL;
    }
    if (base->kind == TYPE_STRUCT) {
        if (base->field_count > 0 && base->field_names != NULL) {
            for (int i = 0; i < base->field_count; i++) {
                if (strcmp(base->field_names[i], field_name) == 0) {
                    return base->field_types[i];
                }
            }
        }
        StructInfo* info = find_struct(tc, base->struct_name);
        if (info != NULL) {
            for (int i = 0; i < info->field_count; i++) {
                if (strcmp(info->field_names[i], field_name) == 0) {
                    return info->field_types[i];
                }
            }
        }
        return NULL;
    }
    if (base->kind == TYPE_ROW) {
        return &type_unknown;
    }
    return NULL;
}

static Type* resolve_named_type(TypeChecker* tc, Type* t) {
    if (t == NULL) return NULL;
    if (t->kind == TYPE_STRUCT && t->struct_name != NULL) {
        for (int i = 0; i < tc->subtype_count; i++) {
            if (strcmp(tc->subtypes[i].name, t->struct_name) == 0) {
                return tc->subtypes[i].base;
            }
        }
    }
    if (t->kind == TYPE_ARRAY && t->element_type != NULL) {
        Type* elem = resolve_named_type(tc, t->element_type);
        if (elem != t->element_type) {
            return transient_array_type(tc, elem);
        }
    }
    if (t->kind == TYPE_MAP && t->map_key_type != NULL) {
        Type* key = resolve_named_type(tc, t->map_key_type);
        Type* val = resolve_named_type(tc, t->element_type);
        if (key != t->map_key_type || val != t->element_type) {
            return transient_map_type(tc, key, val);
        }
    }
    return t;
}

static int add_subtype(TypeChecker* tc, const char* name, Type* base) {
    if (tc->subtype_count >= MAX_SUBTYPES) return 0;
    tc->subtypes[tc->subtype_count].name = name;
    tc->subtypes[tc->subtype_count].base = base;
    tc->subtype_count++;
    return 1;
}

static void check_stmt(TypeChecker* tc, Stmt* stmt);
static Type* infer_expr(TypeChecker* tc, Expr* expr, Type* hint);

static int is_native(const char* name) {
    return strcmp(name, "length") == 0 ||
           strcmp(name, "append") == 0 ||
           strcmp(name, "delete") == 0 ||
           strcmp(name, "first") == 0 ||
           strcmp(name, "last") == 0 ||
           strcmp(name, "next") == 0 ||
           strcmp(name, "prior") == 0 ||
           strcmp(name, "extend") == 0 ||
           strcmp(name, "array_trim") == 0 ||
           strcmp(name, "println") == 0 ||
           strcmp(name, "print") == 0 ||
           strcmp(name, "read_line") == 0 ||
           strcmp(name, "clock") == 0 ||
           strcmp(name, "concat") == 0 ||
           strcmp(name, "substring") == 0 ||
           strcmp(name, "contains") == 0 ||
           strcmp(name, "index_of") == 0 ||
           strcmp(name, "find") == 0 ||
           strcmp(name, "slice") == 0 ||
           strcmp(name, "remove_at") == 0 ||
           strcmp(name, "insert") == 0 ||
           strcmp(name, "to_upper") == 0 ||
           strcmp(name, "to_lower") == 0 ||
           strcmp(name, "trim") == 0 ||
           strcmp(name, "trim_start") == 0 ||
           strcmp(name, "trim_end") == 0 ||
           strcmp(name, "starts_with") == 0 ||
           strcmp(name, "ends_with") == 0 ||
           strcmp(name, "char_at") == 0 ||
           strcmp(name, "reverse_string") == 0 ||
           strcmp(name, "int_to_string") == 0 ||
           strcmp(name, "float_to_string") == 0 ||
           strcmp(name, "abs_int") == 0 ||
           strcmp(name, "abs_float") == 0 ||
           strcmp(name, "min_int") == 0 ||
           strcmp(name, "max_int") == 0 ||
           strcmp(name, "min_float") == 0 ||
           strcmp(name, "max_float") == 0 ||
           strcmp(name, "pow") == 0 ||
           strcmp(name, "sqrt") == 0 ||
           strcmp(name, "round") == 0 ||
           strcmp(name, "floor") == 0 ||
           strcmp(name, "ceil") == 0 ||
           strcmp(name, "mod") == 0 ||
           strcmp(name, "read_file") == 0 ||
           strcmp(name, "write_file") == 0 ||
           strcmp(name, "file_exists") == 0 ||
           strcmp(name, "split") == 0 ||
           strcmp(name, "join") == 0 ||
           strcmp(name, "replace") == 0 ||
           strcmp(name, "repeat") == 0 ||
           strcmp(name, "range") == 0 ||
           strcmp(name, "format") == 0 ||
           strcmp(name, "sort") == 0 ||
           strcmp(name, "reverse") == 0 ||
           strcmp(name, "clamp") == 0 ||
           strcmp(name, "env_get") == 0 ||
           strcmp(name, "sleep") == 0 ||
           strcmp(name, "random_int") == 0 ||
           strcmp(name, "array_fill") == 0 ||
           strcmp(name, "pad_start") == 0 ||
           strcmp(name, "pad_end") == 0 ||
           strcmp(name, "sql_rowcount") == 0 ||
           strcmp(name, "sql_found") == 0 ||
           strcmp(name, "sql_notfound") == 0 ||
           strcmp(name, "execute_immediate") == 0 ||
           strcmp(name, "assert") == 0 ||
           strcmp(name, "parse_int") == 0 ||
           strcmp(name, "split_lines") == 0 ||
           strcmp(name, "join_paths") == 0 ||
           strcmp(name, "is_dir") == 0 ||
           strcmp(name, "list_dir") == 0 ||
           strcmp(name, "mkdir") == 0 ||
           strcmp(name, "is_digit") == 0 ||
           strcmp(name, "is_alpha") == 0 ||
           strcmp(name, "sqlcode") == 0 ||
           strcmp(name, "sqlerrm") == 0 ||
           strcmp(name, "raise_application_error") == 0 ||
           strcmp(name, "dbms_output_enable") == 0 ||
           strcmp(name, "dbms_output_put_line") == 0 ||
           strcmp(name, "dbms_output_disable") == 0 ||
           strcmp(name, "dbms_output_get_lines") == 0 ||
           strcmp(name, "utl_file_fopen") == 0 ||
           strcmp(name, "utl_file_get_line") == 0 ||
           strcmp(name, "utl_file_put_line") == 0 ||
           strcmp(name, "utl_file_fclose") == 0 ||
           strcmp(name, "to_date") == 0 ||
           strcmp(name, "to_char") == 0 ||
           strcmp(name, "current_date") == 0 ||
           strcmp(name, "current_timestamp") == 0;
}

static Type* check_native_call(TypeChecker* tc, const char* name, Expr** args, int arg_count, SourceLoc loc) {
    if (strcmp(name, "length") == 0) {
        if (arg_count != 1) {
            type_error(tc, loc, "length expects 1 argument");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        if (a != &type_unknown && a != NULL &&
            a->kind != TYPE_STRING && !type_is_array(a) && !type_is_map(a)) {
            type_error(tc, loc, "length expects a string, array, or map");
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
    if (strcmp(name, "delete") == 0 ||
        strcmp(name, "first") == 0 ||
        strcmp(name, "last") == 0 ||
        strcmp(name, "next") == 0 ||
        strcmp(name, "prior") == 0) {
        if (strcmp(name, "delete") == 0 || strcmp(name, "next") == 0 || strcmp(name, "prior") == 0) {
            if (arg_count != 2) {
                type_error(tc, loc, "%s expects 2 arguments", name);
                return NULL;
            }
        } else {
            if (arg_count != 1) {
                type_error(tc, loc, "%s expects 1 argument", name);
                return NULL;
            }
        }
        Type* m = infer_expr(tc, args[0], NULL);
        if (m != &type_unknown && m != NULL && !type_is_map(m)) {
            type_error(tc, loc, "%s expects a map", name);
            return &type_unknown;
        }
        if (arg_count == 2) {
            Type* key_type = (m != NULL && type_is_map(m) && m->map_key_type != NULL) ? m->map_key_type : &type_unknown;
            Type* k = infer_expr(tc, args[1], key_type);
            if (m != NULL && type_is_map(m) && m->map_key_type != NULL &&
                k != NULL && k != &type_unknown && !type_equals(m->map_key_type, k)) {
                type_error(tc, loc, "%s key type does not match map key type", name);
            }
        }
        if (m != NULL && type_is_map(m) && m->map_key_type != NULL) {
            return m->map_key_type;
        }
        return &type_unknown;
    }
    if (strcmp(name, "extend") == 0 || strcmp(name, "array_trim") == 0) {
        if (arg_count != 2) {
            type_error(tc, loc, "%s expects 2 arguments", name);
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        if (a != &type_unknown && a != NULL && !type_is_array(a)) {
            type_error(tc, loc, "%s expects an array", name);
            return a;
        }
        Type* n = infer_expr(tc, args[1], NULL);
        if (n != &type_unknown && n != NULL && n->kind != TYPE_INT) {
            type_error(tc, loc, "%s expects an int count", name);
        }
        return a;
    }
    if (strcmp(name, "println") == 0 || strcmp(name, "print") == 0) {
        if (arg_count != 1) {
            type_error(tc, loc, "%s expects 1 argument", name);
            return NULL;
        }
        (void)infer_expr(tc, args[0], NULL);
        return &type_int;
    }
    if (strcmp(name, "read_line") == 0) {
        if (arg_count != 0) {
            type_error(tc, loc, "read_line expects 0 arguments");
            return NULL;
        }
        return &type_string;
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
        if (a != &type_unknown && a != NULL && a->kind != TYPE_STRING && !type_is_array(a)) {
            type_error(tc, loc, "%s expects a string or array as first argument", name);
        }
        if (a != NULL && a->kind == TYPE_STRING && b != &type_unknown && b != NULL && b->kind != TYPE_STRING) {
            type_error(tc, loc, "%s expects string arguments", name);
        }
        return strcmp(name, "contains") == 0 ? &type_bool : &type_int;
    }
    if (strcmp(name, "slice") == 0) {
        if (arg_count != 3) {
            type_error(tc, loc, "slice expects 3 arguments");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        Type* b = infer_expr(tc, args[1], NULL);
        Type* c = infer_expr(tc, args[2], NULL);
        if (a != &type_unknown && a != NULL && !type_is_array(a)) {
            type_error(tc, loc, "slice expects an array");
        }
        if (b != &type_unknown && b != NULL && b->kind != TYPE_INT) {
            type_error(tc, loc, "slice expects int start");
        }
        if (c != &type_unknown && c != NULL && c->kind != TYPE_INT) {
            type_error(tc, loc, "slice expects int end");
        }
        return a;
    }
    if (strcmp(name, "remove_at") == 0) {
        if (arg_count != 2) {
            type_error(tc, loc, "remove_at expects 2 arguments");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        Type* b = infer_expr(tc, args[1], NULL);
        if (a != &type_unknown && a != NULL && !type_is_array(a)) {
            type_error(tc, loc, "remove_at expects an array");
        }
        if (b != &type_unknown && b != NULL && b->kind != TYPE_INT) {
            type_error(tc, loc, "remove_at expects an int index");
        }
        return a;
    }
    if (strcmp(name, "find") == 0) {
        if (arg_count != 2) {
            type_error(tc, loc, "find expects 2 arguments");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        (void)infer_expr(tc, args[1], NULL);
        if (a != &type_unknown && a != NULL && !type_is_array(a)) {
            type_error(tc, loc, "find expects an array");
        }
        return &type_int;
    }
    if (strcmp(name, "insert") == 0) {
        if (arg_count != 3) {
            type_error(tc, loc, "insert expects 3 arguments");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        Type* b = infer_expr(tc, args[1], NULL);
        if (a != &type_unknown && a != NULL && !type_is_array(a)) {
            type_error(tc, loc, "insert expects an array");
        }
        if (b != &type_unknown && b != NULL && b->kind != TYPE_INT) {
            type_error(tc, loc, "insert expects an int index");
        }
        if (a != NULL && type_is_array(a)) {
            (void)infer_expr(tc, args[2], a->element_type);
        } else {
            (void)infer_expr(tc, args[2], NULL);
        }
        return a;
    }
    if (strcmp(name, "to_upper") == 0 ||
        strcmp(name, "to_lower") == 0 ||
        strcmp(name, "trim") == 0 ||
        strcmp(name, "trim_start") == 0 ||
        strcmp(name, "trim_end") == 0 ||
        strcmp(name, "reverse_string") == 0) {
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
    if (strcmp(name, "starts_with") == 0 || strcmp(name, "ends_with") == 0) {
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
        return &type_bool;
    }
    if (strcmp(name, "char_at") == 0) {
        if (arg_count != 2) {
            type_error(tc, loc, "char_at expects 2 arguments");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        Type* b = infer_expr(tc, args[1], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_STRING) {
            type_error(tc, loc, "char_at expects a string");
        }
        if (b != &type_unknown && b != NULL && b->kind != TYPE_INT) {
            type_error(tc, loc, "char_at expects an int index");
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
    if (strcmp(name, "pow") == 0) {
        if (arg_count != 2) {
            type_error(tc, loc, "pow expects 2 arguments");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        Type* b = infer_expr(tc, args[1], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_INT && a->kind != TYPE_FLOAT) {
            type_error(tc, loc, "pow expects numeric arguments");
        }
        if (b != &type_unknown && b != NULL && b->kind != TYPE_INT && b->kind != TYPE_FLOAT) {
            type_error(tc, loc, "pow expects numeric arguments");
        }
        return &type_float;
    }
    if (strcmp(name, "sqrt") == 0 || strcmp(name, "round") == 0 ||
        strcmp(name, "floor") == 0 || strcmp(name, "ceil") == 0) {
        if (arg_count != 1) {
            type_error(tc, loc, "%s expects 1 argument", name);
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_INT && a->kind != TYPE_FLOAT) {
            type_error(tc, loc, "%s expects a number", name);
        }
        if (strcmp(name, "sqrt") == 0) {
            return &type_float;
        }
        return &type_int;
    }
    if (strcmp(name, "mod") == 0) {
        if (arg_count != 2) {
            type_error(tc, loc, "mod expects 2 arguments");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        Type* b = infer_expr(tc, args[1], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_INT) {
            type_error(tc, loc, "mod expects int arguments");
        }
        if (b != &type_unknown && b != NULL && b->kind != TYPE_INT) {
            type_error(tc, loc, "mod expects int arguments");
        }
        return &type_int;
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
    if (strcmp(name, "range") == 0) {
        if (arg_count != 2) {
            type_error(tc, loc, "range expects 2 arguments");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        Type* b = infer_expr(tc, args[1], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_INT) {
            type_error(tc, loc, "range expects int arguments");
        }
        if (b != &type_unknown && b != NULL && b->kind != TYPE_INT) {
            type_error(tc, loc, "range expects int arguments");
        }
        return transient_array_type(tc, &type_int);
    }
    if (strcmp(name, "assert") == 0) {
        if (arg_count != 2) {
            type_error(tc, loc, "assert expects 2 arguments");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        Type* b = infer_expr(tc, args[1], NULL);
        if (a != &type_unknown && a != NULL &&
            a->kind != TYPE_BOOL && a->kind != TYPE_INT) {
            type_error(tc, loc, "assert expects bool or int condition");
        }
        if (b != &type_unknown && b != NULL && b->kind != TYPE_STRING) {
            type_error(tc, loc, "assert expects string message");
        }
        return &type_int;
    }
    if (strcmp(name, "parse_int") == 0) {
        if (arg_count != 1) {
            type_error(tc, loc, "parse_int expects 1 argument");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_STRING) {
            type_error(tc, loc, "parse_int expects a string");
        }
        return &type_int;
    }
    if (strcmp(name, "split_lines") == 0) {
        if (arg_count != 1) {
            type_error(tc, loc, "split_lines expects 1 argument");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_STRING) {
            type_error(tc, loc, "split_lines expects a string");
        }
        return transient_array_type(tc, &type_string);
    }
    if (strcmp(name, "join_paths") == 0) {
        if (arg_count != 2) {
            type_error(tc, loc, "join_paths expects 2 arguments");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        Type* b = infer_expr(tc, args[1], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_STRING) {
            type_error(tc, loc, "join_paths expects string arguments");
        }
        if (b != &type_unknown && b != NULL && b->kind != TYPE_STRING) {
            type_error(tc, loc, "join_paths expects string arguments");
        }
        return &type_string;
    }
    if (strcmp(name, "is_dir") == 0 || strcmp(name, "mkdir") == 0) {
        if (arg_count != 1) {
            type_error(tc, loc, "%s expects 1 argument", name);
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_STRING) {
            type_error(tc, loc, "%s expects a string path", name);
        }
        return &type_bool;
    }
    if (strcmp(name, "list_dir") == 0) {
        if (arg_count != 1) {
            type_error(tc, loc, "list_dir expects 1 argument");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_STRING) {
            type_error(tc, loc, "list_dir expects a string path");
        }
        return transient_array_type(tc, &type_string);
    }
    if (strcmp(name, "is_digit") == 0 || strcmp(name, "is_alpha") == 0) {
        if (arg_count != 1) {
            type_error(tc, loc, "%s expects 1 argument", name);
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_STRING) {
            type_error(tc, loc, "%s expects a string", name);
        }
        return &type_bool;
    }
    if (strcmp(name, "format") == 0) {
        if (arg_count != 2) {
            type_error(tc, loc, "format expects 2 arguments");
            return NULL;
        }
        Type* fmt = infer_expr(tc, args[0], NULL);
        Type* vals = infer_expr(tc, args[1], transient_array_type(tc, &type_string));
        if (fmt != &type_unknown && fmt != NULL && fmt->kind != TYPE_STRING) {
            type_error(tc, loc, "format expects a string format");
        }
        if (vals != &type_unknown && vals != NULL && !type_is_array(vals)) {
            type_error(tc, loc, "format expects an array<string>");
        }
        if (vals != NULL && type_is_array(vals) &&
            vals->element_type != NULL &&
            vals->element_type->kind != TYPE_UNKNOWN &&
            vals->element_type->kind != TYPE_STRING) {
            type_error(tc, loc, "format expects an array<string>");
        }
        return &type_string;
    }
    if (strcmp(name, "sort") == 0) {
        if (arg_count != 1) {
            type_error(tc, loc, "sort expects 1 argument");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        if (a != &type_unknown && a != NULL && !type_is_array(a)) {
            type_error(tc, loc, "sort expects an array");
        }
        if (a != NULL && type_is_array(a) &&
            a->element_type != NULL &&
            a->element_type->kind != TYPE_UNKNOWN &&
            a->element_type->kind != TYPE_INT &&
            a->element_type->kind != TYPE_FLOAT &&
            a->element_type->kind != TYPE_STRING) {
            type_error(tc, loc, "sort expects array<int>, array<float>, or array<string>");
        }
        return a;
    }
    if (strcmp(name, "reverse") == 0) {
        if (arg_count != 1) {
            type_error(tc, loc, "reverse expects 1 argument");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        if (a != &type_unknown && a != NULL && !type_is_array(a)) {
            type_error(tc, loc, "reverse expects an array");
        }
        return a;
    }
    if (strcmp(name, "clamp") == 0) {
        if (arg_count != 3) {
            type_error(tc, loc, "clamp expects 3 arguments");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        Type* b = infer_expr(tc, args[1], NULL);
        Type* c = infer_expr(tc, args[2], NULL);
        int has_float = 0;
        Type* types[3] = {a, b, c};
        for (int i = 0; i < 3; i++) {
            Type* t = types[i];
            if (t == &type_unknown || t == NULL) continue;
            if (t->kind == TYPE_FLOAT) has_float = 1;
            if (t->kind != TYPE_INT && t->kind != TYPE_FLOAT) {
                type_error(tc, loc, "clamp expects numeric arguments");
                break;
            }
        }
        return has_float ? &type_float : &type_int;
    }
    if (strcmp(name, "env_get") == 0) {
        if (arg_count != 1) {
            type_error(tc, loc, "env_get expects 1 argument");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_STRING) {
            type_error(tc, loc, "env_get expects a string");
        }
        return &type_string;
    }
    if (strcmp(name, "sleep") == 0) {
        if (arg_count != 1) {
            type_error(tc, loc, "sleep expects 1 argument");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_INT) {
            type_error(tc, loc, "sleep expects an int");
        }
        return &type_int;
    }
    if (strcmp(name, "random_int") == 0) {
        if (arg_count != 2) {
            type_error(tc, loc, "random_int expects 2 arguments");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        Type* b = infer_expr(tc, args[1], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_INT) {
            type_error(tc, loc, "random_int expects int arguments");
        }
        if (b != &type_unknown && b != NULL && b->kind != TYPE_INT) {
            type_error(tc, loc, "random_int expects int arguments");
        }
        return &type_int;
    }
    if (strcmp(name, "array_fill") == 0) {
        if (arg_count != 2) {
            type_error(tc, loc, "array_fill expects 2 arguments");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        Type* b = infer_expr(tc, args[1], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_INT) {
            type_error(tc, loc, "array_fill expects an int count");
        }
        return transient_array_type(tc, b != NULL ? b : &type_unknown);
    }
    if (strcmp(name, "pad_start") == 0 || strcmp(name, "pad_end") == 0) {
        if (arg_count != 3) {
            type_error(tc, loc, "%s expects 3 arguments", name);
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        Type* b = infer_expr(tc, args[1], NULL);
        Type* c = infer_expr(tc, args[2], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_STRING) {
            type_error(tc, loc, "%s expects a string", name);
        }
        if (b != &type_unknown && b != NULL && b->kind != TYPE_INT) {
            type_error(tc, loc, "%s expects an int length", name);
        }
        if (c != &type_unknown && c != NULL && c->kind != TYPE_STRING) {
            type_error(tc, loc, "%s expects a string pad", name);
        }
        return &type_string;
    }
    if (strcmp(name, "sql_rowcount") == 0) {
        if (arg_count != 0) {
            type_error(tc, loc, "sql_rowcount expects 0 arguments");
            return NULL;
        }
        return &type_int;
    }
    if (strcmp(name, "execute_immediate") == 0) {
        if (arg_count != 1) {
            type_error(tc, loc, "execute_immediate expects 1 argument");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_STRING) {
            type_error(tc, loc, "execute_immediate expects a string");
        }
        return &type_int;
    }
    if (strcmp(name, "sql_found") == 0 || strcmp(name, "sql_notfound") == 0) {
        if (arg_count != 0) {
            type_error(tc, loc, "%s expects 0 arguments", name);
            return NULL;
        }
        return &type_bool;
    }
    if (strcmp(name, "sqlcode") == 0) {
        if (arg_count != 0) {
            type_error(tc, loc, "sqlcode expects 0 arguments");
            return NULL;
        }
        return &type_int;
    }
    if (strcmp(name, "sqlerrm") == 0) {
        if (arg_count != 0) {
            type_error(tc, loc, "sqlerrm expects 0 arguments");
            return NULL;
        }
        return &type_string;
    }
    if (strcmp(name, "raise_application_error") == 0) {
        if (arg_count != 2) {
            type_error(tc, loc, "raise_application_error expects 2 arguments");
            return NULL;
        }
        Type* code = infer_expr(tc, args[0], NULL);
        Type* msg = infer_expr(tc, args[1], NULL);
        if (code != &type_unknown && code != NULL && code->kind != TYPE_INT) {
            type_error(tc, loc, "raise_application_error expects an int code");
        }
        if (msg != &type_unknown && msg != NULL && msg->kind != TYPE_STRING) {
            type_error(tc, loc, "raise_application_error expects a string message");
        }
        return &type_int;
    }
    if (strcmp(name, "to_date") == 0) {
        if (arg_count != 2) {
            type_error(tc, loc, "to_date expects 2 arguments");
            return NULL;
        }
        Type* s = infer_expr(tc, args[0], NULL);
        Type* fmt = infer_expr(tc, args[1], NULL);
        if (s != &type_unknown && s != NULL && s->kind != TYPE_STRING) {
            type_error(tc, loc, "to_date expects a string date");
        }
        if (fmt != &type_unknown && fmt != NULL && fmt->kind != TYPE_STRING) {
            type_error(tc, loc, "to_date expects a string format");
        }
        return &type_date;
    }
    if (strcmp(name, "to_char") == 0) {
        if (arg_count != 2) {
            type_error(tc, loc, "to_char expects 2 arguments");
            return NULL;
        }
        Type* v = infer_expr(tc, args[0], NULL);
        Type* fmt = infer_expr(tc, args[1], NULL);
        if (v != &type_unknown && v != NULL && v->kind != TYPE_DATE && v->kind != TYPE_TIMESTAMP) {
            type_error(tc, loc, "to_char expects a date or timestamp");
        }
        if (fmt != &type_unknown && fmt != NULL && fmt->kind != TYPE_STRING) {
            type_error(tc, loc, "to_char expects a string format");
        }
        return &type_string;
    }
    if (strcmp(name, "current_date") == 0) {
        if (arg_count != 0) {
            type_error(tc, loc, "current_date expects 0 arguments");
            return NULL;
        }
        return &type_date;
    }
    if (strcmp(name, "current_timestamp") == 0) {
        if (arg_count != 0) {
            type_error(tc, loc, "current_timestamp expects 0 arguments");
            return NULL;
        }
        return &type_timestamp;
    }
    if (strcmp(name, "dbms_output_enable") == 0) {
        if (arg_count != 1) {
            type_error(tc, loc, "dbms_output_enable expects 1 argument");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_INT) {
            type_error(tc, loc, "dbms_output_enable expects an int");
        }
        return &type_int;
    }
    if (strcmp(name, "dbms_output_put_line") == 0) {
        if (arg_count != 1) {
            type_error(tc, loc, "dbms_output_put_line expects 1 argument");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_STRING) {
            type_error(tc, loc, "dbms_output_put_line expects a string");
        }
        return &type_int;
    }
    if (strcmp(name, "dbms_output_disable") == 0) {
        if (arg_count != 0) {
            type_error(tc, loc, "dbms_output_disable expects 0 arguments");
            return NULL;
        }
        return &type_int;
    }
    if (strcmp(name, "dbms_output_get_lines") == 0) {
        if (arg_count != 0) {
            type_error(tc, loc, "dbms_output_get_lines expects 0 arguments");
            return NULL;
        }
        return transient_array_type(tc, &type_string);
    }
    if (strcmp(name, "utl_file_fopen") == 0) {
        if (arg_count != 2) {
            type_error(tc, loc, "utl_file_fopen expects 2 arguments");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_STRING) {
            type_error(tc, loc, "utl_file_fopen expects a string path");
        }
        Type* b = infer_expr(tc, args[1], NULL);
        if (b != &type_unknown && b != NULL && b->kind != TYPE_STRING) {
            type_error(tc, loc, "utl_file_fopen expects a string mode");
        }
        return &type_int;
    }
    if (strcmp(name, "utl_file_get_line") == 0) {
        if (arg_count != 1) {
            type_error(tc, loc, "utl_file_get_line expects 1 argument");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_INT) {
            type_error(tc, loc, "utl_file_get_line expects an int handle");
        }
        return &type_string;
    }
    if (strcmp(name, "utl_file_put_line") == 0) {
        if (arg_count != 2) {
            type_error(tc, loc, "utl_file_put_line expects 2 arguments");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_INT) {
            type_error(tc, loc, "utl_file_put_line expects an int handle");
        }
        Type* b = infer_expr(tc, args[1], NULL);
        if (b != &type_unknown && b != NULL && b->kind != TYPE_STRING) {
            type_error(tc, loc, "utl_file_put_line expects a string");
        }
        return &type_int;
    }
    if (strcmp(name, "utl_file_fclose") == 0) {
        if (arg_count != 1) {
            type_error(tc, loc, "utl_file_fclose expects 1 argument");
            return NULL;
        }
        Type* a = infer_expr(tc, args[0], NULL);
        if (a != &type_unknown && a != NULL && a->kind != TYPE_INT) {
            type_error(tc, loc, "utl_file_fclose expects an int handle");
        }
        return &type_int;
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
                case VAL_INT:       return &type_int;
                case VAL_FLOAT:     return &type_float;
                case VAL_STRING:    return &type_string;
                case VAL_BOOL:      return &type_bool;
                case VAL_DATE:      return &type_date;
                case VAL_TIMESTAMP: return &type_timestamp;
                case VAL_ARRAY:     return &type_int; /* unreachable for literals */
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
            const char* package_name = expr->as.call.package_name;
            ProcSignature* sig = NULL;

            if (package_name != NULL) {
                sig = resolve_proc_in_package(tc, package_name, call_name);
                if (sig == NULL) {
                    type_error(tc, expr->loc, "Undefined package member '%s.%s'",
                               package_name, call_name);
                    return NULL;
                }
                if (tc->current_package == NULL ||
                    strcmp(tc->current_package, package_name) != 0) {
                    if (!package_has_public_member(tc->program, package_name, call_name)) {
                        type_error(tc, expr->loc,
                                   "Package member '%s.%s' is private",
                                   package_name, call_name);
                        return sig->return_type;
                    }
                }
            } else {
                if (is_native(call_name)) {
                    return check_native_call(tc, call_name, expr->as.call.args,
                                             expr->as.call.arg_count, expr->loc);
                }
                if (tc->current_package != NULL) {
                    sig = resolve_proc_in_package(tc, tc->current_package, call_name);
                }
                if (sig == NULL) {
                    sig = resolve_proc(tc, call_name);
                }
                if (sig == NULL) {
                    type_error(tc, expr->loc, "Undefined procedure '%s'",
                               call_name);
                    return NULL;
                }
            }
            if (expr->as.call.arg_count != sig->param_count) {
                type_error(tc, expr->loc,
                           "Procedure '%s' expects %d arguments but got %d",
                           expr->as.call.name, sig->param_count,
                           expr->as.call.arg_count);
                return sig->return_type;
            }
            for (int i = 0; i < expr->as.call.arg_count; i++) {
                if (sig->param_modes != NULL &&
                    (sig->param_modes[i] == PARAM_OUT ||
                     sig->param_modes[i] == PARAM_INOUT)) {
                    if (expr->as.call.args[i]->kind != EXPR_VARIABLE) {
                        type_error(tc, expr->as.call.args[i]->loc,
                                   "Argument %d of '%s' must be a variable for OUT/IN OUT parameter",
                                   i + 1, expr->as.call.name);
                        return sig->return_type;
                    }
                }
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
            if (base == NULL) {
                type_error(tc, expr->loc, "Cannot index value with no type");
                return NULL;
            }
            if (base->kind == TYPE_ARRAY) {
                if (idx == NULL || idx->kind != TYPE_INT) {
                    type_error(tc, expr->loc, "Array index must be int");
                    return NULL;
                }
                return base->element_type;
            }
            if (base->kind == TYPE_MAP) {
                Type* expected_key = base->map_key_type != NULL ? base->map_key_type : &type_string;
                if (idx == NULL || !type_equals(idx, expected_key)) {
                    type_error(tc, expr->loc, "Map key must be %s", type_name(expected_key));
                    return NULL;
                }
                return base->element_type;
            }
            type_error(tc, expr->loc, "Cannot index non-array type");
            return NULL;
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

        case EXPR_CURSOR_ATTR: {
            Type* t = resolve_local(tc, expr->as.cursor_attr.cursor_name);
            if (t == NULL) {
                type_error(tc, expr->loc, "Undefined variable '%s'",
                           expr->as.cursor_attr.cursor_name);
                return &type_unknown;
            }
            if (t != &type_unknown && t->kind != TYPE_CURSOR) {
                type_error(tc, expr->loc, "Cursor attribute '%s' requires a cursor variable",
                           expr->as.cursor_attr.attr_name);
                return &type_unknown;
            }
            const char* attr = expr->as.cursor_attr.attr_name;
            if (strcmp(attr, "rowcount") == 0) {
                return &type_int;
            }
            if (strcmp(attr, "found") == 0 ||
                strcmp(attr, "notfound") == 0 ||
                strcmp(attr, "isopen") == 0) {
                return &type_bool;
            }
            type_error(tc, expr->loc, "Unknown cursor attribute '%s'", attr);
            return &type_unknown;
        }

        case EXPR_ROW_FIELD: {
            Type* base = infer_expr(tc, expr->as.row_field.row, NULL);
            if (tc->had_error) return NULL;
            Type* ft = resolve_field_type(tc, base, expr->as.row_field.field, expr->loc);
            if (tc->had_error) return &type_unknown;
            if (ft != NULL) return ft;
            if (base == NULL || (base->kind != TYPE_STRUCT && base->kind != TYPE_ROW &&
                                 base->kind != TYPE_PERCENT_ROWTYPE)) {
                type_error(tc, expr->loc, "Cannot access field on non-row expression");
            } else {
                type_error(tc, expr->loc, "Unknown field '%s'", expr->as.row_field.field);
            }
            return &type_unknown;
        }

        case EXPR_STRUCT_LITERAL: {
            StructLiteralExpr* sl = &expr->as.struct_literal;
            StructInfo* info = find_struct(tc, sl->struct_name);
            if (info == NULL) {
                type_error(tc, expr->loc, "Unknown struct '%s'", sl->struct_name);
                return &type_unknown;
            }
            for (int i = 0; i < sl->field_count; i++) {
                Type* expected = struct_field_type(tc, sl->struct_name, sl->field_names[i]);
                if (expected == NULL) {
                    type_error(tc, expr->loc, "Unknown field '%s' on struct '%s'",
                               sl->field_names[i], sl->struct_name);
                    return &type_unknown;
                }
                Type* actual = infer_expr(tc, sl->values[i], expected);
                if (tc->had_error) return &type_unknown;
                if (!types_assignable(expected, actual)) {
                    type_error(tc, sl->values[i]->loc,
                               "Field '%s' expects %s but got %s",
                               sl->field_names[i], type_name(expected), type_name(actual));
                    return &type_unknown;
                }
            }
            return make_struct_type(tc, sl->struct_name);
        }

        case EXPR_MAP_LITERAL: {
            MapLiteralExpr* m = &expr->as.map_literal;
            Type* key_type = NULL;
            Type* value_type = NULL;
            if (m->count == 0) {
                if (hint != NULL && hint->kind == TYPE_MAP) {
                    key_type = hint->map_key_type;
                    value_type = hint->element_type;
                } else {
                    type_error(tc, expr->loc, "Cannot infer value type of empty map");
                    return transient_map_type(tc, &type_string, &type_unknown);
                }
            } else if (hint != NULL && hint->kind == TYPE_MAP) {
                key_type = hint->map_key_type;
                value_type = hint->element_type;
            }
            for (int i = 0; i < m->count; i++) {
                Type* inferred_key = infer_expr(tc, m->keys[i], NULL);
                if (tc->had_error) return NULL;
                if (inferred_key != NULL && inferred_key != &type_unknown &&
                    inferred_key->kind != TYPE_STRING && inferred_key->kind != TYPE_INT) {
                    type_error(tc, m->keys[i]->loc, "Map key must be int or string");
                    return NULL;
                }
                if (key_type == NULL) {
                    key_type = inferred_key;
                } else if (inferred_key != NULL && inferred_key != &type_unknown &&
                           !type_equals(key_type, inferred_key)) {
                    type_error(tc, m->keys[i]->loc, "Map key type mismatch");
                    return NULL;
                }
                Type* val = infer_expr(tc, m->values[i], value_type);
                if (tc->had_error) return NULL;
                if (value_type == NULL) {
                    value_type = val;
                } else if (val != NULL && val != &type_unknown &&
                           !type_equals(value_type, val)) {
                    type_error(tc, m->values[i]->loc, "Map value type mismatch");
                    return NULL;
                }
            }
            if (key_type == NULL) key_type = &type_string;
            if (value_type == NULL) value_type = &type_unknown;
            return transient_map_type(tc, key_type, value_type);
        }
    }

    return NULL;
}

static void check_stmt(TypeChecker* tc, Stmt* stmt) {
    switch (stmt->kind) {
        case STMT_VAR_DECL: {
            Type* declared = resolve_percent_type(tc, stmt->as.var_decl.type, stmt->loc);
            Type* final_type = resolve_named_type(tc, declared);

            if (stmt->as.var_decl.initializer != NULL) {
                Type* init_type = infer_expr(tc, stmt->as.var_decl.initializer, final_type);
                if (tc->had_error) return;
                if (init_type == NULL) {
                    type_error(tc, stmt->loc, "Cannot infer type of initializer");
                    return;
                }

                if (final_type != NULL && final_type->kind == TYPE_ARRAY &&
                    final_type->element_type == NULL &&
                    init_type->kind == TYPE_ARRAY) {
                    final_type = init_type;
                } else if (!types_assignable(final_type, init_type)) {
                    type_error(tc, stmt->loc,
                               "Cannot assign value of type '%s' to variable of type '%s'",
                               type_name(init_type), type_name(final_type));
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

        case STMT_FIELD_ASSIGN: {
            FieldAssignStmt* fa = &stmt->as.field_assign;
            Type* base = infer_expr(tc, fa->object, NULL);
            if (tc->had_error) return;
            Type* ft = resolve_field_type(tc, base, fa->field, stmt->loc);
            if (tc->had_error) return;
            if (ft == NULL) {
                type_error(tc, stmt->loc, "Cannot assign to field '%s'", fa->field);
                return;
            }
            Type* rhs = infer_expr(tc, fa->value, ft);
            if (tc->had_error) return;
            if (!types_assignable(ft, rhs)) {
                type_error(tc, stmt->loc,
                           "Cannot assign value of type '%s' to field of type '%s'",
                           type_name(rhs), type_name(ft));
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

        case STMT_WHILE: {
            Type* cond = infer_expr(tc, stmt->as.while_stmt.condition, NULL);
            if (tc->had_error) return;
            if (!is_condition_type(cond)) {
                type_error(tc, stmt->loc,
                           "While condition must be bool or numeric");
                return;
            }
            check_block(tc, stmt->as.while_stmt.body);
            break;
        }

        case STMT_BREAK:
        case STMT_CONTINUE:
            break;

        case STMT_FOR_C: {
            CForStmt* cf = &stmt->as.cfor_stmt;
            if (cf->init != NULL) {
                check_stmt(tc, cf->init);
                if (tc->had_error) return;
            }
            if (cf->condition != NULL) {
                Type* cond = infer_expr(tc, cf->condition, NULL);
                if (tc->had_error) return;
                if (!is_condition_type(cond)) {
                    type_error(tc, stmt->loc,
                               "For condition must be bool or numeric");
                    return;
                }
            }
            if (cf->step != NULL) {
                check_stmt(tc, cf->step);
                if (tc->had_error) return;
            }
            check_block(tc, cf->body);
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

        case STMT_FOREACH: {
            if (!push_scope(tc)) {
                type_error(tc, stmt->loc, "Too many nested scopes");
                return;
            }
            ForeachStmt* f = &stmt->as.foreach_stmt;
            Type* iterable = infer_expr(tc, f->iterable, NULL);
            if (tc->had_error) {
                pop_scope(tc);
                return;
            }
            if (iterable != NULL && iterable != &type_unknown &&
                !type_is_array(iterable)) {
                type_error(tc, stmt->loc, "For-in requires an array");
                pop_scope(tc);
                return;
            }
            Type* element_type = (iterable != NULL && type_is_array(iterable))
                                     ? iterable->element_type
                                     : &type_unknown;
            if (!add_local(tc, f->var_name, element_type)) {
                type_error(tc, stmt->loc, "Too many local variables");
                pop_scope(tc);
                return;
            }
            check_block(tc, f->body);
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
                                    base != NULL &&
                                        (base->kind == TYPE_ARRAY || base->kind == TYPE_MAP)
                                        ? base->element_type : NULL);
            if (tc->had_error) return;
            if (base == NULL) {
                type_error(tc, stmt->loc, "Cannot index assign to value with no type");
                return;
            }
            if (base->kind == TYPE_ARRAY) {
                if (idx == NULL || idx->kind != TYPE_INT) {
                    type_error(tc, stmt->loc, "Array index must be int");
                    return;
                }
                if (!types_assignable(base->element_type, val)) {
                    type_error(tc, stmt->loc,
                               "Cannot assign value of type '%s' to array element of type '%s'",
                               type_name(val), type_name(base->element_type));
                }
                return;
            }
            if (base->kind == TYPE_MAP) {
                Type* expected_key = base->map_key_type != NULL ? base->map_key_type : &type_string;
                if (idx == NULL || !type_equals(idx, expected_key)) {
                    type_error(tc, stmt->loc, "Map key must be %s", type_name(expected_key));
                    return;
                }
                if (!types_assignable(base->element_type, val)) {
                    type_error(tc, stmt->loc,
                               "Cannot assign value of type '%s' to map value of type '%s'",
                               type_name(val), type_name(base->element_type));
                }
                return;
            }
            type_error(tc, stmt->loc, "Cannot index assign to non-array type");
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
        case STMT_FORALL: {
            ForallStmt* f = &stmt->as.forall_stmt;
            Type* array_type = resolve_local(tc, f->array_name);
            if (array_type == NULL) {
                type_error(tc, stmt->loc, "Undefined variable '%s'", f->array_name);
                return;
            }
            if (array_type != &type_unknown && !type_is_array(array_type)) {
                type_error(tc, stmt->loc, "FORALL requires an array variable");
                return;
            }
            Type* element_type = (array_type != NULL && type_is_array(array_type))
                                     ? array_type->element_type
                                     : &type_unknown;
            if (!push_scope(tc)) {
                type_error(tc, stmt->loc, "too many nested scopes");
                return;
            }
            if (!add_local(tc, f->var_name, element_type)) {
                type_error(tc, stmt->loc, "too many local variables");
                pop_scope(tc);
                return;
            }
            check_stmt(tc, f->sql_stmt);
            pop_scope(tc);
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

        case STMT_TRY_CATCH: {
            TryCatchStmt* tcs = &stmt->as.try_catch;
            check_block(tc, tcs->try_block);
            if (tc->had_error) return;
            if (!push_scope(tc)) {
                type_error(tc, (SourceLoc){0, 0}, "too many nested scopes");
                return;
            }
            if (!add_local(tc, tcs->catch_var, &type_string)) {
                type_error(tc, (SourceLoc){0, 0}, "too many locals");
                pop_scope(tc);
                return;
            }
            check_block(tc, tcs->catch_block);
            pop_scope(tc);
            break;
        }
        case STMT_EXCEPTION_DECL: {
            if (!add_exception(tc, stmt->as.exception_decl.name)) {
                type_error(tc, stmt->loc, "too many exceptions");
            }
            break;
        }
        case STMT_RAISE: {
            if (!resolve_exception(tc, stmt->as.raise_stmt.name)) {
                type_error(tc, stmt->loc, "Undefined exception '%s'",
                           stmt->as.raise_stmt.name);
            }
            break;
        }
        case STMT_SUBTYPE_DECL: {
            SubtypeDeclStmt* d = &stmt->as.subtype_decl;
            Type* base = resolve_named_type(tc, d->base_type);
            if (base == NULL) {
                type_error(tc, stmt->loc, "Unknown base type for subtype '%s'", d->name);
                return;
            }
            if (!add_subtype(tc, d->name, base)) {
                type_error(tc, stmt->loc, "Too many subtype declarations");
            }
            break;
        }
        case STMT_CASE: {
            CaseStmt* cs = &stmt->as.case_stmt;
            Type* selector_type = infer_expr(tc, cs->selector, NULL);
            if (tc->had_error) return;
            for (int i = 0; i < cs->branch_count; i++) {
                Type* value_type = infer_expr(tc, cs->values[i], NULL);
                if (tc->had_error) return;
                if (!type_equals(selector_type, value_type)) {
                    type_error(tc, cs->values[i]->loc,
                               "case value type does not match selector type");
                    return;
                }
            }
            for (int i = 0; i < cs->branch_count; i++) {
                check_block(tc, cs->blocks[i]);
                if (tc->had_error) return;
            }
            if (cs->else_block != NULL) {
                check_block(tc, cs->else_block);
            }
            break;
        }
        case STMT_SQL_TRANSACTION: {
            /* no type checking needed */
            break;
        }

        case STMT_CURSOR_DECL: {
            CursorDeclStmt* d = &stmt->as.cursor_decl;
            if (!add_local(tc, d->name, &type_cursor)) {
                type_error(tc, stmt->loc, "Too many local variables");
                return;
            }
            if (d->sql_query != NULL) {
                if (!bind_cursor(tc, d->name, d->sql_query)) {
                    type_error(tc, stmt->loc, "too many cursor bindings");
                }
            }
            break;
        }

        case STMT_CURSOR_OPEN: {
            CursorOpenStmt* o = &stmt->as.cursor_open;
            Type* t = resolve_local(tc, o->name);
            if (t == NULL) {
                type_error(tc, stmt->loc, "Undefined variable '%s'", o->name);
                return;
            }
            if (t != &type_unknown && t->kind != TYPE_CURSOR) {
                type_error(tc, stmt->loc, "OPEN requires a cursor variable");
                return;
            }
            for (int i = 0; i < o->param_count; i++) {
                infer_expr(tc, o->params[i], NULL);
                if (tc->had_error) return;
            }
            if (o->sql_query != NULL) {
                if (!bind_cursor(tc, o->name, o->sql_query)) {
                    type_error(tc, stmt->loc, "too many cursor bindings");
                }
            }
            break;
        }

        case STMT_CURSOR_FETCH: {
            CursorFetchStmt* f = &stmt->as.cursor_fetch;
            Type* t = resolve_local(tc, f->name);
            if (t == NULL) {
                type_error(tc, stmt->loc, "Undefined variable '%s'", f->name);
                return;
            }
            if (t != &type_unknown && t->kind != TYPE_CURSOR) {
                type_error(tc, stmt->loc, "FETCH requires a cursor variable");
                return;
            }
            for (int i = 0; i < f->into_count; i++) {
                Type* vt = resolve_local(tc, f->into_vars[i]);
                if (vt == NULL) {
                    type_error(tc, stmt->loc, "Undefined variable '%s'", f->into_vars[i]);
                    return;
                }
                if (vt != &type_unknown &&
                    vt->kind != TYPE_INT && vt->kind != TYPE_FLOAT &&
                    vt->kind != TYPE_STRING && vt->kind != TYPE_BOOL) {
                    type_error(tc, stmt->loc, "FETCH target must be a scalar variable");
                    return;
                }
            }
            break;
        }

        case STMT_CURSOR_CLOSE: {
            CursorCloseStmt* c = &stmt->as.cursor_close;
            Type* t = resolve_local(tc, c->name);
            if (t == NULL) {
                type_error(tc, stmt->loc, "Undefined variable '%s'", c->name);
                return;
            }
            if (t != &type_unknown && t->kind != TYPE_CURSOR) {
                type_error(tc, stmt->loc, "CLOSE requires a cursor variable");
                return;
            }
            break;
        }
        case STMT_PRAGMA:
            /* Pragmas are handled during code generation. */
            break;
    }
}

int typecheck_program(Program* program,
                      ProcSignature* procs,
                      int proc_count,
                      struct Context* ctx,
                      const char* source_path,
                      char* error,
                      size_t error_size) {
    if (error != NULL && error_size > 0) {
        error[0] = '\0';
    }

    if (program == NULL) {
        if (error != NULL && error_size > 0) {
            format_error(error, error_size, source_path, 0, 0,
                         "Type error: program is NULL");
        }
        return 0;
    }

    TypeChecker tc = {0};
    tc.ctx = ctx;
    tc.source_path = source_path;
    tc.error = error;
    tc.error_size = error_size;
    tc.program = program;

    /* Predefined exceptions mirror Oracle PL/SQL codes. */
    add_exception(&tc, "no_data_found");
    add_exception(&tc, "too_many_rows");

    for (int i = 0; i < program->struct_count && tc.struct_count < MAX_STRUCTS; i++) {
        StructDecl* decl = &program->structs[i];
        StructInfo* info = &tc.structs[tc.struct_count++];
        info->name = decl->name;
        info->field_names = decl->field_names;
        info->field_types = decl->field_types;
        info->field_count = decl->field_count;
    }

    if (procs == NULL && proc_count > 0) {
        proc_count = 0;
    }

    ProcSignature* combined_procs = NULL;
    int combined_count = program->proc_count + proc_count;
    if (combined_count > 0) {
        combined_procs = malloc(sizeof(ProcSignature) * combined_count);
        if (combined_procs == NULL) {
            if (error != NULL && error_size > 0) {
                format_error(error, error_size, source_path, 0, 0,
                             "Type error: out of memory");
            }
            return 0;
        }
        for (int i = 0; i < program->proc_count; i++) {
            ProcDecl* proc = &program->procs[i];
            Type** param_types = NULL;
            ParamMode* param_modes = NULL;
            if (proc->param_count > 0) {
                param_types = malloc(sizeof(Type*) * proc->param_count);
                param_modes = malloc(sizeof(ParamMode) * proc->param_count);
                if (param_types == NULL || param_modes == NULL) {
                    free(param_types);
                    free(param_modes);
                    for (int j = 0; j < i; j++) {
                        free(combined_procs[j].param_types);
                        free(combined_procs[j].param_modes);
                    }
                    free(combined_procs);
                    if (error != NULL && error_size > 0) {
                        format_error(error, error_size, source_path, 0, 0,
                                     "Type error: out of memory");
                    }
                    return 0;
                }
            }
            for (int p = 0; p < proc->param_count; p++) {
                param_types[p] = resolve_named_type(&tc, proc->params[p].type);
                param_modes[p] = proc->params[p].mode;
            }
            combined_procs[i].name = proc->name;
            combined_procs[i].return_type = resolve_named_type(&tc, proc->return_type);
            combined_procs[i].param_types = param_types;
            combined_procs[i].param_modes = param_modes;
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
            if (!add_local(&tc, proc->params[p].name, resolve_named_type(&tc, proc->params[p].type))) {
                type_error(&tc, (SourceLoc){0, 0}, "Too many local variables");
                break;
            }
        }
        if (tc.had_error) {
            pop_scope(&tc);
            break;
        }
        tc.return_type = resolve_named_type(&tc, proc->return_type);
        check_block(&tc, proc->body);
        pop_scope(&tc);
        if (tc.had_error) break;
    }

    for (int b = 0; b < program->body_count && !tc.had_error; b++) {
        PackageBodyDecl* body = &program->bodies[b];
        tc.current_package = body->name;
        if (!push_scope(&tc)) {
            type_error(&tc, (SourceLoc){0, 0}, "Too many nested scopes");
            break;
        }
        for (int v = 0; v < body->var_count; v++) {
            VarDeclStmt* var = &body->vars[v]->as.var_decl;
            if (!add_local(&tc, var->name, resolve_named_type(&tc, var->type))) {
                type_error(&tc, (SourceLoc){0, 0}, "Too many local variables");
                break;
            }
        }
        if (tc.had_error) {
            pop_scope(&tc);
            break;
        }
        for (int v = 0; v < body->var_count && !tc.had_error; v++) {
            check_stmt(&tc, body->vars[v]);
        }
        if (tc.had_error) {
            pop_scope(&tc);
            break;
        }

        for (int i = 0; i < body->proc_count + body->func_count && !tc.had_error; i++) {
            ProcDecl* proc = (i < body->proc_count) ? &body->procs[i] : &body->funcs[i - body->proc_count];
            if (!push_scope(&tc)) {
                type_error(&tc, (SourceLoc){0, 0}, "Too many nested scopes");
                break;
            }
            for (int p = 0; p < proc->param_count; p++) {
                if (!add_local(&tc, proc->params[p].name, resolve_named_type(&tc, proc->params[p].type))) {
                    type_error(&tc, (SourceLoc){0, 0}, "Too many local variables");
                    break;
                }
            }
            if (tc.had_error) {
                pop_scope(&tc);
                break;
            }
            tc.return_type = resolve_named_type(&tc, proc->return_type);
            check_block(&tc, proc->body);
            pop_scope(&tc);
        }
        pop_scope(&tc);
    }
    tc.current_package = NULL;

    for (int i = 0; i < tc.transient_count; i++) {
        type_free(tc.transient_types[i]);
    }

    for (int i = 0; i < program->proc_count; i++) {
        free(combined_procs[i].param_types);
        free(combined_procs[i].param_modes);
    }
    free(combined_procs);

    return !tc.had_error;
}
