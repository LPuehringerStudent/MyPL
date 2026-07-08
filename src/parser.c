#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diagnostics.h"
#include "parser.h"
#include "lexer.h"

#define MAX_IMPORTS 64
#define MAX_PARSER_STRUCTS 64

typedef struct {
    Lexer lexer;
    Token current;
    Token previous;
    int had_error;
    const char* path;
    char error_message[256];
    char* struct_names[MAX_PARSER_STRUCTS];
    int struct_count;
} Parser;

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,
    PREC_OR,
    PREC_AND,
    PREC_EQUALITY,
    PREC_COMPARISON,
    PREC_TERM,
    PREC_FACTOR,
    PREC_UNARY,
    PREC_CALL,
    PREC_PRIMARY
} Precedence;

typedef Expr* (*PrefixFn)(Parser* parser);
typedef Expr* (*InfixFn)(Parser* parser, Expr* left);

typedef struct {
    PrefixFn prefix;
    InfixFn infix;
    Precedence precedence;
} ParseRule;

static ParseRule* get_rule(TokenType type);
static Expr* parse_precedence(Parser* parser, Precedence precedence);

static void error_at_current(Parser* parser, const char* message) {
    char msg[256];
    snprintf(msg, sizeof(msg), "%s (got token %d)", message, parser->current.type);
    format_error(parser->error_message, sizeof(parser->error_message),
                 parser->path, parser->current.line, parser->current.column, msg);
    parser->had_error = 1;
}

static void error_at_previous(Parser* parser, const char* message) {
    char msg[256];
    snprintf(msg, sizeof(msg), "%s (got token %d)", message, parser->previous.type);
    format_error(parser->error_message, sizeof(parser->error_message),
                 parser->path, parser->previous.line, parser->previous.column, msg);
    parser->had_error = 1;
}

static void advance(Parser* parser) {
    parser->previous = parser->current;
    parser->current = lexer_next_token(&parser->lexer);
}

static int check(Parser* parser, TokenType type) {
    return parser->current.type == type;
}

static int match(Parser* parser, TokenType type) {
    if (!check(parser, type)) return 0;
    advance(parser);
    return 1;
}

static char* copy_token_lexeme(const Token* token) {
    char* lexeme = malloc((size_t)token->length + 1);
    memcpy(lexeme, token->start, (size_t)token->length);
    lexeme[token->length] = '\0';
    return lexeme;
}

static int parser_is_struct_name(Parser* parser, const char* name) {
    for (int i = 0; i < parser->struct_count; i++) {
        if (strcmp(parser->struct_names[i], name) == 0) return 1;
    }
    return 0;
}

static int parser_add_struct_name(Parser* parser, const char* name) {
    if (parser->struct_count >= MAX_PARSER_STRUCTS) return 0;
    char* copy = malloc(strlen(name) + 1);
    if (copy == NULL) return 0;
    strcpy(copy, name);
    parser->struct_names[parser->struct_count++] = copy;
    return 1;
}

static void parser_free_struct_names(Parser* parser) {
    for (int i = 0; i < parser->struct_count; i++) {
        free(parser->struct_names[i]);
    }
    parser->struct_count = 0;
}

static Expr* expression(Parser* parser);

static int is_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static int is_word_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static int ascii_case_eq(char a, char b) {
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    return a == b;
}

static const char* find_word(const char* s, const char* word) {
    size_t len = strlen(word);
    for (const char* p = s; *p != '\0'; p++) {
        if ((p == s || !is_word_char(p[-1])) && !is_word_char(p[len])) {
            int match = 1;
            for (size_t i = 0; i < len; i++) {
                if (!ascii_case_eq(p[i], word[i])) {
                    match = 0;
                    break;
                }
            }
            if (match) return p;
        }
    }
    return NULL;
}

static Stmt* sql_statement(Parser* parser, int kind) {
    Token start_token = parser->previous;
    const char* sql_start = start_token.start;
    while (!check(parser, TOKEN_SEMICOLON) && !check(parser, TOKEN_EOF)) {
        advance(parser);
    }
    if (check(parser, TOKEN_EOF)) {
        error_at_current(parser, "expected ';' after SQL statement");
        return NULL;
    }
    int sql_len = (int)(parser->current.start - sql_start);
    char* sql = malloc((size_t)sql_len + 1);
    if (sql == NULL) {
        error_at_current(parser, "out of memory");
        return NULL;
    }
    memcpy(sql, sql_start, (size_t)sql_len);
    sql[sql_len] = '\0';

    Expr** params = NULL;
    int param_count = 0;
    char* out = sql;
    const char* p = sql;
    while (*p != '\0') {
        if (*p == '?') {
            const char* ident = p + 1;
            const char* q = ident;
            while (is_ident_char(*q)) q++;
            if (q > ident) {
                int name_len = (int)(q - ident);
                char* name = malloc((size_t)name_len + 1);
                if (name == NULL) {
                    free(sql);
                    for (int i = 0; i < param_count; i++) free_expr(params[i]);
                    free(params);
                    error_at_current(parser, "out of memory");
                    return NULL;
                }
                memcpy(name, ident, (size_t)name_len);
                name[name_len] = '\0';
                Expr** new_params = realloc(params, sizeof(Expr*) * (size_t)(param_count + 1));
                if (new_params == NULL) {
                    free(name);
                    free(sql);
                    for (int i = 0; i < param_count; i++) free_expr(params[i]);
                    free(params);
                    error_at_current(parser, "out of memory");
                    return NULL;
                }
                params = new_params;
                params[param_count++] = create_sql_param_expr(name);
                free(name);

                /* Copy '?' and skip identifier in output SQL. */
                *out++ = '?';
                p = q;
                continue;
            }
        }
        *out++ = *p++;
    }
    *out = '\0';

    char** into_vars = NULL;
    int into_count = 0;
    char* final_sql = sql;
    if (kind == STMT_SQL_QUERY) {
        const char* into_pos = find_word(sql, "into");
        if (into_pos != NULL) {
            const char* from_pos = find_word(into_pos + 4, "from");
            if (from_pos == NULL) {
                free(sql);
                for (int i = 0; i < param_count; i++) free_expr(params[i]);
                free(params);
                error_at_current(parser, "expected FROM after INTO in SELECT statement");
                return NULL;
            }
            const char* list_start = into_pos + 4;
            while (*list_start != '\0' && isspace((unsigned char)*list_start)) list_start++;
            const char* list_end = from_pos;
            while (list_end > list_start && isspace((unsigned char)list_end[-1])) list_end--;
            if (list_end == list_start) {
                free(sql);
                for (int i = 0; i < param_count; i++) free_expr(params[i]);
                free(params);
                error_at_current(parser, "expected variable name after INTO");
                return NULL;
            }

            const char* p = list_start;
            while (p < list_end) {
                while (p < list_end && isspace((unsigned char)*p)) p++;
                const char* part_start = p;
                while (p < list_end && *p != ',') p++;
                const char* part_end = p;
                while (part_end > part_start && isspace((unsigned char)part_end[-1])) part_end--;
                if (part_end == part_start) {
                    for (int i = 0; i < into_count; i++) free(into_vars[i]);
                    free(into_vars);
                    free(sql);
                    for (int i = 0; i < param_count; i++) free_expr(params[i]);
                    free(params);
                    error_at_current(parser, "expected variable name in INTO list");
                    return NULL;
                }
                int var_len = (int)(part_end - part_start);
                char* var_name = malloc((size_t)var_len + 1);
                if (var_name == NULL) {
                    for (int i = 0; i < into_count; i++) free(into_vars[i]);
                    free(into_vars);
                    free(sql);
                    for (int i = 0; i < param_count; i++) free_expr(params[i]);
                    free(params);
                    error_at_current(parser, "out of memory");
                    return NULL;
                }
                memcpy(var_name, part_start, (size_t)var_len);
                var_name[var_len] = '\0';
                char** new_vars = realloc(into_vars, sizeof(char*) * (size_t)(into_count + 1));
                if (new_vars == NULL) {
                    free(var_name);
                    for (int i = 0; i < into_count; i++) free(into_vars[i]);
                    free(into_vars);
                    free(sql);
                    for (int i = 0; i < param_count; i++) free_expr(params[i]);
                    free(params);
                    error_at_current(parser, "out of memory");
                    return NULL;
                }
                into_vars = new_vars;
                into_vars[into_count++] = var_name;
                if (p < list_end) p++; /* skip comma */
            }

            int prefix_len = (int)(into_pos - sql);
            int suffix_len = (int)strlen(from_pos);
            int new_len = prefix_len + 5 + suffix_len; /* " FROM " + suffix, but keep one space before FROM */
            final_sql = malloc((size_t)new_len + 1);
            if (final_sql == NULL) {
                for (int i = 0; i < into_count; i++) free(into_vars[i]);
                free(into_vars);
                free(sql);
                for (int i = 0; i < param_count; i++) free_expr(params[i]);
                free(params);
                error_at_current(parser, "out of memory");
                return NULL;
            }
            /* Trim trailing whitespace from prefix. */
            while (prefix_len > 0 && isspace((unsigned char)sql[prefix_len - 1])) prefix_len--;
            memcpy(final_sql, sql, (size_t)prefix_len);
            final_sql[prefix_len] = ' ';
            memcpy(final_sql + prefix_len + 1, from_pos, (size_t)suffix_len);
            final_sql[prefix_len + 1 + suffix_len] = '\0';
        }
    }

    Stmt* stmt = create_sql_stmt(kind, final_sql, params, param_count, into_vars, into_count);
    if (stmt != NULL) {
        stmt->loc.line = start_token.line;
        stmt->loc.column = start_token.column;
    }
    if (final_sql != sql) free(final_sql);
    free(sql);
    if (stmt == NULL) {
        for (int i = 0; i < param_count; i++) free_expr(params[i]);
        free(params);
        for (int i = 0; i < into_count; i++) free(into_vars[i]);
        free(into_vars);
        error_at_current(parser, "out of memory");
        return NULL;
    }
    advance(parser); /* consume ; */
    return stmt;
}

static Expr* grouping(Parser* parser) {
    Expr* expr = expression(parser);
    advance(parser); /* consume ) */
    return expr;
}

static Expr* unary(Parser* parser) {
    Token op = parser->previous;
    Expr* operand = parse_precedence(parser, PREC_UNARY);
    Expr* expr = create_unary_expr(op.type, operand);
    expr->loc.line = op.line;
    expr->loc.column = op.column;
    return expr;
}

/* Convert a raw string literal body (without quotes) into an unescaped
   C string. The returned pointer must be freed by the caller. */
static char* unescape_string(const char* raw, int len) {
    char* out = malloc((size_t)len + 1);
    if (out == NULL) return NULL;
    int j = 0;
    for (int i = 0; i < len; i++) {
        if (raw[i] == '\\' && i + 1 < len) {
            switch (raw[i + 1]) {
                case 'n': out[j++] = '\n'; i++; break;
                case 't': out[j++] = '\t'; i++; break;
                case 'r': out[j++] = '\r'; i++; break;
                case '\\': out[j++] = '\\'; i++; break;
                case '"': out[j++] = '"'; i++; break;
                default: out[j++] = raw[i]; break;
            }
        } else {
            out[j++] = raw[i];
        }
    }
    out[j] = '\0';
    return out;
}

static Expr* literal_bool(Parser* parser) {
    int v = parser->previous.type == TOKEN_TRUE ? 1 : 0;
    Expr* expr = create_literal_expr(value_bool(v));
    expr->loc.line = parser->previous.line;
    expr->loc.column = parser->previous.column;
    return expr;
}

static Expr* number(Parser* parser) {
    Expr* expr;
    if (parser->previous.type == TOKEN_FLOAT) {
        char buffer[64];
        int len = parser->previous.length;
        if (len >= 64) len = 63;
        memcpy(buffer, parser->previous.start, (size_t)len);
        buffer[len] = '\0';
        expr = create_literal_expr(value_float(strtod(buffer, NULL)));
    } else if (parser->previous.type == TOKEN_STRING) {
        int len = parser->previous.length - 2; /* without quotes */
        if (len < 0) len = 0;
        char* str = unescape_string(parser->previous.start + 1, len);
        if (str == NULL) return create_literal_expr(value_int(0));
        expr = create_literal_expr(value_string(str));
    } else {
        int value = 0;
        for (int i = 0; i < parser->previous.length; i++) {
            value = value * 10 + (parser->previous.start[i] - '0');
        }
        expr = create_literal_expr(value_int(value));
    }
    expr->loc.line = parser->previous.line;
    expr->loc.column = parser->previous.column;
    return expr;
}

static Expr* struct_literal(Parser* parser, char* struct_name) {
    advance(parser); /* { */
    char** field_names = NULL;
    Expr** values = NULL;
    int field_count = 0;
    if (!check(parser, TOKEN_RBRACE)) {
        do {
            if (!check(parser, TOKEN_IDENT)) {
                error_at_current(parser, "expected field name");
                for (int i = 0; i < field_count; i++) {
                    free(field_names[i]);
                    free_expr(values[i]);
                }
                free(field_names);
                free(values);
                free(struct_name);
                return NULL;
            }
            advance(parser); /* field name */
            char** new_names = malloc(sizeof(char*) * (size_t)(field_count + 1));
            Expr** new_values = malloc(sizeof(Expr*) * (size_t)(field_count + 1));
            if (new_names == NULL || new_values == NULL) {
                for (int i = 0; i < field_count; i++) {
                    free(field_names[i]);
                    free_expr(values[i]);
                }
                free(field_names);
                free(values);
                free(new_names);
                free(new_values);
                free(struct_name);
                return NULL;
            }
            if (field_count > 0) {
                memcpy(new_names, field_names, sizeof(char*) * (size_t)field_count);
                memcpy(new_values, values, sizeof(Expr*) * (size_t)field_count);
                free(field_names);
                free(values);
            }
            field_names = new_names;
            values = new_values;
            field_names[field_count] = copy_token_lexeme(&parser->previous);
            advance(parser); /* = */
            values[field_count] = expression(parser);
            field_count++;
        } while (match(parser, TOKEN_COMMA));
    }
    if (!match(parser, TOKEN_RBRACE)) {
        error_at_current(parser, "expected '}' after struct literal");
        for (int i = 0; i < field_count; i++) {
            free(field_names[i]);
            free_expr(values[i]);
        }
        free(field_names);
        free(values);
        free(struct_name);
        return NULL;
    }
    Expr* expr = create_struct_literal_expr(struct_name, field_names, values, field_count);
    free(struct_name);
    return expr;
}

static Expr* map_literal(Parser* parser) {
    /* '{' was already consumed by the prefix dispatcher */
    Expr** keys = NULL;
    Expr** values = NULL;
    int count = 0;
    if (!check(parser, TOKEN_RBRACE)) {
        do {
            Expr** new_keys = malloc(sizeof(Expr*) * (size_t)(count + 1));
            Expr** new_values = malloc(sizeof(Expr*) * (size_t)(count + 1));
            if (new_keys == NULL || new_values == NULL) {
                for (int i = 0; i < count; i++) {
                    free_expr(keys[i]);
                    free_expr(values[i]);
                }
                free(keys);
                free(values);
                free(new_keys);
                free(new_values);
                return NULL;
            }
            if (count > 0) {
                memcpy(new_keys, keys, sizeof(Expr*) * (size_t)count);
                memcpy(new_values, values, sizeof(Expr*) * (size_t)count);
                free(keys);
                free(values);
            }
            keys = new_keys;
            values = new_values;
            keys[count] = expression(parser);
            if (!match(parser, TOKEN_COLON)) {
                error_at_current(parser, "expected ':' after map key");
                for (int i = 0; i <= count; i++) {
                    free_expr(keys[i]);
                    free_expr(values[i]);
                }
                free(keys);
                free(values);
                return NULL;
            }
            values[count] = expression(parser);
            count++;
        } while (match(parser, TOKEN_COMMA));
    }
    if (!match(parser, TOKEN_RBRACE)) {
        error_at_current(parser, "expected '}' after map literal");
        for (int i = 0; i < count; i++) {
            free_expr(keys[i]);
            free_expr(values[i]);
        }
        free(keys);
        free(values);
        return NULL;
    }
    Expr* expr = create_map_literal_expr(keys, values, count);
    expr->loc.line = parser->previous.line;
    expr->loc.column = parser->previous.column;
    return expr;
}

static Expr* variable(Parser* parser) {
    char* name = copy_token_lexeme(&parser->previous);
    if (check(parser, TOKEN_LBRACE) && parser_is_struct_name(parser, name)) {
        return struct_literal(parser, name);
    }
    Expr* expr = create_variable_expr(name);
    free(name);
    expr->loc.line = parser->previous.line;
    expr->loc.column = parser->previous.column;
    return expr;
}

static Expr* field(Parser* parser, Expr* left) {
    advance(parser); /* field name */
    char* field_name = copy_token_lexeme(&parser->previous);
    Expr* expr;
    if (left->kind == EXPR_VARIABLE && strcmp(left->as.variable.name, "row") == 0) {
        expr = create_field_expr(left->as.variable.name, field_name);
        free_expr(left);
    } else {
        expr = create_row_field_expr(left, field_name);
    }
    expr->loc.line = parser->previous.line;
    expr->loc.column = parser->previous.column;
    free(field_name);
    return expr;
}

static Expr* call(Parser* parser, Expr* left) {
    if (left->kind != EXPR_VARIABLE) {
        error_at_current(parser, "can only call named procedures");
        return left;
    }

    Expr** args = NULL;
    int arg_count = 0;
    if (!check(parser, TOKEN_RPAREN)) {
        do {
            Expr** new_args = realloc(args, sizeof(Expr*) * (size_t)(arg_count + 1));
            if (new_args == NULL) {
                for (int i = 0; i < arg_count; i++) free_expr(args[i]);
                free(args);
                free_expr(left);
                return NULL;
            }
            args = new_args;
            args[arg_count++] = expression(parser);
        } while (match(parser, TOKEN_COMMA));
    }
    advance(parser); /* consume ) */

    Expr* expr = create_call_expr(left->as.variable.name, args, arg_count);
    expr->loc.line = parser->previous.line;
    expr->loc.column = parser->previous.column;
    free_expr(left);
    return expr;
}

static Expr* array_literal(Parser* parser) {
    Expr** elements = NULL;
    int count = 0;
    if (!check(parser, TOKEN_RBRACKET)) {
        do {
            Expr** new_elements = realloc(elements, sizeof(Expr*) * (size_t)(count + 1));
            if (new_elements == NULL) {
                for (int i = 0; i < count; i++) {
                    free_expr(elements[i]);
                }
                free(elements);
                return NULL;
            }
            elements = new_elements;
            elements[count++] = expression(parser);
        } while (match(parser, TOKEN_COMMA));
    }
    advance(parser); /* ] */
    Expr* expr = create_array_expr(elements, count);
    expr->loc.line = parser->previous.line;
    expr->loc.column = parser->previous.column;
    return expr;
}

static Expr* index_expr(Parser* parser, Expr* left) {
    Expr* idx = expression(parser);
    advance(parser); /* ] */
    Expr* expr = create_index_expr(left, idx);
    expr->loc.line = parser->previous.line;
    expr->loc.column = parser->previous.column;
    return expr;
}

static Expr* binary(Parser* parser, Expr* left) {
    Token op = parser->previous;
    ParseRule* rule = get_rule(op.type);
    Expr* right = parse_precedence(parser, (Precedence)(rule->precedence + 1));
    Expr* expr = create_binary_expr(op.type, left, right);
    expr->loc.line = op.line;
    expr->loc.column = op.column;
    return expr;
}

static Expr* parse_precedence(Parser* parser, Precedence precedence) {
    advance(parser);
    PrefixFn prefix_rule = get_rule(parser->previous.type)->prefix;
    if (prefix_rule == NULL) {
        error_at_previous(parser, "expected expression");
        return NULL;
    }

    Expr* left = prefix_rule(parser);

    while (precedence <= get_rule(parser->current.type)->precedence) {
        advance(parser);
        InfixFn infix_rule = get_rule(parser->previous.type)->infix;
        left = infix_rule(parser, left);
    }

    return left;
}

static Expr* expression(Parser* parser) {
    return parse_precedence(parser, PREC_ASSIGNMENT);
}

static Type* parse_type(Parser* parser) {
    if (match(parser, TOKEN_INT_TYPE)) return &type_int;
    if (match(parser, TOKEN_FLOAT_TYPE)) return &type_float;
    if (match(parser, TOKEN_STRING_TYPE)) return &type_string;
    if (match(parser, TOKEN_BOOL_TYPE)) return &type_bool;
    if (check(parser, TOKEN_IDENT) && parser->current.length == 3 &&
        strncmp(parser->current.start, "row", 3) == 0) {
        advance(parser);
        return &type_row;
    }
    if (check(parser, TOKEN_IDENT)) {
        Type* t = type_new(TYPE_STRUCT, NULL);
        if (t == NULL) {
            error_at_current(parser, "out of memory");
            return &type_int;
        }
        t->struct_name = copy_token_lexeme(&parser->current);
        advance(parser);
        return t;
    }
    if (match(parser, TOKEN_ARRAY_TYPE)) {
        if (match(parser, TOKEN_LT)) {
            Type* element = parse_type(parser);
            if (!match(parser, TOKEN_GT)) {
                error_at_current(parser, "expected '>' after array element type");
            }
            return type_new(TYPE_ARRAY, element);
        }
        return type_new(TYPE_ARRAY, NULL);  /* generic array */
    }
    if (match(parser, TOKEN_MAP_TYPE)) {
        Type* value_type = &type_unknown;
        if (match(parser, TOKEN_LT)) {
            Type* key_type = parse_type(parser);
            if (key_type == NULL || key_type->kind != TYPE_STRING) {
                error_at_current(parser, "map key type must be string");
            }
            if (!match(parser, TOKEN_COMMA)) {
                error_at_current(parser, "expected ',' between map key and value types");
            }
            value_type = parse_type(parser);
            if (!match(parser, TOKEN_GT)) {
                error_at_current(parser, "expected '>' after map value type");
            }
        }
        return type_new(TYPE_MAP, value_type);
    }
    error_at_current(parser, "expected type");
    return &type_int;
}

static Stmt* var_decl(Parser* parser);
static Stmt* statement(Parser* parser);

static Block* block(Parser* parser) {
    Block* result = create_block();
    advance(parser); /* { */
    while (!check(parser, TOKEN_RBRACE) && !check(parser, TOKEN_EOF) &&
           !parser->had_error) {
        Stmt** new_stmts = realloc(result->stmts,
            sizeof(Stmt*) * (size_t)(result->stmt_count + 1));
        result->stmts = new_stmts;
        result->stmts[result->stmt_count++] = statement(parser);
    }
    if (parser->had_error) {
        free_block(result);
        return NULL;
    }
    if (!check(parser, TOKEN_RBRACE)) {
        error_at_current(parser, "expected '}'");
        free_block(result);
        return NULL;
    }
    advance(parser); /* } */
    return result;
}

static Stmt* var_decl(Parser* parser) {
    Type* type = parse_type(parser);
    advance(parser); /* identifier */
    int id_line = parser->previous.line;
    int id_column = parser->previous.column;
    char* name = copy_token_lexeme(&parser->previous);
    Expr* init = NULL;
    if (match(parser, TOKEN_ASSIGN)) {
        init = expression(parser);
    }
    advance(parser); /* semicolon */
    Stmt* stmt = create_var_decl_stmt(type, name, init);
    stmt->loc.line = id_line;
    stmt->loc.column = id_column;
    free(name);
    return stmt;
}

static Stmt* assignment(Parser* parser) {
    advance(parser); /* identifier */
    int id_line = parser->previous.line;
    int id_column = parser->previous.column;
    char* name = copy_token_lexeme(&parser->previous);

    if (check(parser, TOKEN_LPAREN)) {
        advance(parser); /* ( */
        Expr* call_expr = call(parser, create_variable_expr(name));
        advance(parser); /* ; */
        Stmt* stmt = create_expr_stmt(call_expr);
        stmt->loc.line = call_expr->loc.line;
        stmt->loc.column = call_expr->loc.column;
        free(name);
        return stmt;
    }

    if (match(parser, TOKEN_LBRACKET)) {
        Expr* array_expr = create_variable_expr(name);
        Expr* index_expr = expression(parser);
        advance(parser); /* ] */
        while (match(parser, TOKEN_LBRACKET)) {
            array_expr = create_index_expr(array_expr, index_expr);
            index_expr = expression(parser);
            advance(parser); /* ] */
        }
        advance(parser); /* = */
        Expr* value = expression(parser);
        advance(parser); /* ; */
        Stmt* stmt = create_index_assign_stmt(array_expr, index_expr, value);
        stmt->loc.line = id_line;
        stmt->loc.column = id_column;
        free(name);
        return stmt;
    }

    advance(parser); /* = */
    Expr* value = expression(parser);
    advance(parser); /* ; */
    Stmt* stmt = create_assign_stmt(name, value);
    stmt->loc.line = id_line;
    stmt->loc.column = id_column;
    free(name);
    return stmt;
}

static Stmt* if_statement(Parser* parser) {
    Token kw = parser->previous;  /* 'if' was just consumed */
    Expr* cond = expression(parser);
    Block* then_block = block(parser);
    if (then_block == NULL) {
        free_expr(cond);
        return NULL;
    }
    Block* else_block = NULL;
    if (match(parser, TOKEN_ELSE)) {
        if (check(parser, TOKEN_IF)) {
            advance(parser); /* consume 'if' */
            Stmt* nested = if_statement(parser);
            if (nested == NULL) {
                free_expr(cond);
                free_block(then_block);
                return NULL;
            }
            else_block = create_block();
            if (else_block == NULL) {
                free_stmt(nested);
                free_expr(cond);
                free_block(then_block);
                return NULL;
            }
            Stmt** stmts = malloc(sizeof(Stmt*));
            if (stmts == NULL) {
                free_block(else_block);
                free_stmt(nested);
                free_expr(cond);
                free_block(then_block);
                return NULL;
            }
            stmts[0] = nested;
            else_block->stmts = stmts;
            else_block->stmt_count = 1;
        } else {
            else_block = block(parser);
            if (else_block == NULL) {
                free_expr(cond);
                free_block(then_block);
                return NULL;
            }
        }
    }
    Stmt* stmt = create_if_stmt(cond, then_block, else_block);
    stmt->loc.line = kw.line;
    stmt->loc.column = kw.column;
    return stmt;
}

static Stmt* while_statement(Parser* parser) {
    Token kw = parser->previous;  /* 'while' was just consumed */
    Expr* cond = expression(parser);
    Block* body = block(parser);
    if (body == NULL) {
        free_expr(cond);
        return NULL;
    }
    Stmt* stmt = create_while_stmt(cond, body);
    stmt->loc.line = kw.line;
    stmt->loc.column = kw.column;
    return stmt;
}

static Stmt* do_while_statement(Parser* parser) {
    Token kw = parser->previous;  /* 'do' was just consumed */
    Block* body = block(parser);
    if (body == NULL) return NULL;
    if (!check(parser, TOKEN_WHILE)) {
        error_at_current(parser, "expected 'while' after 'do' body");
        free_block(body);
        return NULL;
    }
    advance(parser); /* consume 'while' */
    Expr* cond = expression(parser);
    if (cond == NULL) {
        free_block(body);
        return NULL;
    }
    if (!check(parser, TOKEN_SEMICOLON)) {
        error_at_current(parser, "expected ';' after do-while condition");
        free_expr(cond);
        free_block(body);
        return NULL;
    }
    advance(parser); /* consume ';' */
    Stmt* stmt = create_do_while_stmt(cond, body);
    stmt->loc.line = kw.line;
    stmt->loc.column = kw.column;
    return stmt;
}

static Stmt* break_statement(Parser* parser) {
    Token kw = parser->previous;  /* 'break' was just consumed */
    advance(parser); /* ; */
    Stmt* stmt = create_break_stmt();
    stmt->loc.line = kw.line;
    stmt->loc.column = kw.column;
    return stmt;
}

static Stmt* continue_statement(Parser* parser) {
    Token kw = parser->previous;  /* 'continue' was just consumed */
    advance(parser); /* ; */
    Stmt* stmt = create_continue_stmt();
    stmt->loc.line = kw.line;
    stmt->loc.column = kw.column;
    return stmt;
}

static Stmt* sql_for_statement(Parser* parser, Token kw, char* var_name) {
    Token sql_token = parser->current;
    advance(parser); /* SQL query */

    int sql_len = sql_token.length;
    while (sql_len > 0) {
        char c = sql_token.start[sql_len - 1];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
        sql_len--;
    }
    char* sql = malloc((size_t)sql_len + 1);
    if (sql == NULL) {
        free(var_name);
        error_at_current(parser, "out of memory");
        return NULL;
    }
    memcpy(sql, sql_token.start, (size_t)sql_len);
    sql[sql_len] = '\0';

    Expr** params = NULL;
    int param_count = 0;
    char* out = sql;
    const char* p = sql;
    while (*p != '\0') {
        if (*p == '?') {
            const char* ident = p + 1;
            const char* q = ident;
            while (is_ident_char(*q)) q++;
            if (q > ident) {
                int name_len = (int)(q - ident);
                char* name = malloc((size_t)name_len + 1);
                if (name == NULL) {
                    free(sql);
                    for (int i = 0; i < param_count; i++) free_expr(params[i]);
                    free(params);
                    free(var_name);
                    error_at_current(parser, "out of memory");
                    return NULL;
                }
                memcpy(name, ident, (size_t)name_len);
                name[name_len] = '\0';
                Expr** new_params = realloc(params, sizeof(Expr*) * (size_t)(param_count + 1));
                if (new_params == NULL) {
                    free(name);
                    free(sql);
                    for (int i = 0; i < param_count; i++) free_expr(params[i]);
                    free(params);
                    free(var_name);
                    error_at_current(parser, "out of memory");
                    return NULL;
                }
                params = new_params;
                params[param_count++] = create_sql_param_expr(name);
                free(name);

                *out++ = '?';
                p = q;
                continue;
            }
        }
        *out++ = *p++;
    }
    *out = '\0';

    Block* body = block(parser);
    if (body == NULL) {
        free(var_name);
        free(sql);
        for (int i = 0; i < param_count; i++) free_expr(params[i]);
        free(params);
        return NULL;
    }
    Stmt* stmt = create_for_stmt(var_name, sql, params, param_count, body);
    stmt->loc.line = kw.line;
    stmt->loc.column = kw.column;
    free(var_name);
    free(sql);
    return stmt;
}

static Stmt* for_step(Parser* parser);

static Stmt* cfor_statement(Parser* parser, Token kw) {
    /* '(' was already consumed */
    Stmt* init = NULL;
    if (!check(parser, TOKEN_SEMICOLON)) {
        init = statement(parser);
        if (init == NULL) return NULL;
    } else {
        advance(parser); /* consume ';' for empty init */
    }

    Expr* cond = NULL;
    if (!check(parser, TOKEN_SEMICOLON)) {
        cond = expression(parser);
        if (cond == NULL) {
            if (init != NULL) free_stmt(init);
            return NULL;
        }
    }
    if (!check(parser, TOKEN_SEMICOLON)) {
        error_at_current(parser, "expected ';' after for condition");
        if (init != NULL) free_stmt(init);
        if (cond != NULL) free_expr(cond);
        return NULL;
    }
    advance(parser); /* consume ';' */

    Stmt* step = NULL;
    if (!check(parser, TOKEN_RPAREN)) {
        step = for_step(parser);
        if (step == NULL) {
            if (init != NULL) free_stmt(init);
            if (cond != NULL) free_expr(cond);
            return NULL;
        }
    }
    if (!check(parser, TOKEN_RPAREN)) {
        error_at_current(parser, "expected ')' after for clauses");
        if (init != NULL) free_stmt(init);
        if (cond != NULL) free_expr(cond);
        if (step != NULL) free_stmt(step);
        return NULL;
    }
    advance(parser); /* consume ')' */

    Block* body = block(parser);
    if (body == NULL) {
        if (init != NULL) free_stmt(init);
        if (cond != NULL) free_expr(cond);
        if (step != NULL) free_stmt(step);
        return NULL;
    }
    Stmt* stmt = create_cfor_stmt(init, cond, step, body);
    stmt->loc.line = kw.line;
    stmt->loc.column = kw.column;
    return stmt;
}

static Stmt* for_step(Parser* parser) {
    if (check(parser, TOKEN_IDENT)) {
        Token ident = parser->current;
        advance(parser);
        if (check(parser, TOKEN_ASSIGN)) {
            advance(parser); /* = */
            Expr* value = expression(parser);
            if (value == NULL) return NULL;
            Stmt* stmt = create_assign_stmt(copy_token_lexeme(&ident), value);
            stmt->loc.line = ident.line;
            stmt->loc.column = ident.column;
            return stmt;
        }
        if (check(parser, TOKEN_LPAREN)) {
            Expr* call_expr = call(parser, create_variable_expr(copy_token_lexeme(&ident)));
            Stmt* stmt = create_expr_stmt(call_expr);
            stmt->loc.line = ident.line;
            stmt->loc.column = ident.column;
            return stmt;
        }
        error_at_current(parser, "expected assignment or call in for step");
        return NULL;
    }
    Expr* expr = expression(parser);
    if (expr == NULL) return NULL;
    Stmt* stmt = create_expr_stmt(expr);
    return stmt;
}

static Stmt* for_statement(Parser* parser) {
    Token kw = parser->previous;  /* 'for' was just consumed */
    if (match(parser, TOKEN_LPAREN)) {
        return cfor_statement(parser, kw);
    }
    advance(parser); /* iterator name */
    char* var_name = copy_token_lexeme(&parser->previous);
    if (!match(parser, TOKEN_IN)) {
        free(var_name);
        error_at_current(parser, "expected 'in' after iterator variable");
        return NULL;
    }
    if (check(parser, TOKEN_SQL_QUERY)) {
        return sql_for_statement(parser, kw, var_name);
    }
    Expr* iterable = expression(parser);
    if (iterable == NULL) {
        free(var_name);
        return NULL;
    }
    Block* body = block(parser);
    if (body == NULL) {
        free_expr(iterable);
        free(var_name);
        return NULL;
    }
    Stmt* stmt = create_foreach_stmt(var_name, iterable, body);
    stmt->loc.line = kw.line;
    stmt->loc.column = kw.column;
    free(var_name);
    return stmt;
}

static Stmt* return_statement(Parser* parser) {
    /* 'return' was already consumed by the statement dispatcher */
    int kw_line = parser->previous.line;
    int kw_column = parser->previous.column;
    Expr* value = expression(parser);
    advance(parser); /* ; */
    Stmt* stmt = create_return_stmt(value);
    stmt->loc.line = kw_line;
    stmt->loc.column = kw_column;
    return stmt;
}

static Stmt* print_statement(Parser* parser) {
    /* 'print' was already consumed by the statement dispatcher */
    int kw_line = parser->previous.line;
    int kw_column = parser->previous.column;
    Expr* value = expression(parser);
    advance(parser); /* ; */
    Stmt* stmt = create_print_stmt(value);
    stmt->loc.line = kw_line;
    stmt->loc.column = kw_column;
    return stmt;
}

static int program_add_import(Program* program, Stmt* import_stmt) {
    if (program->import_count >= MAX_IMPORTS) {
        free_stmt(import_stmt);
        return 0;
    }
    if (program->import_count >= program->import_capacity) {
        int new_capacity = program->import_capacity == 0 ? 4 : program->import_capacity * 2;
        if (new_capacity > MAX_IMPORTS) new_capacity = MAX_IMPORTS;
        Stmt** new_imports = realloc(program->imports, sizeof(Stmt*) * (size_t)new_capacity);
        if (new_imports == NULL) {
            free_stmt(import_stmt);
            return 0;
        }
        program->imports = new_imports;
        program->import_capacity = new_capacity;
    }
    program->imports[program->import_count++] = import_stmt;
    return 1;
}

static void parse_import(Parser* parser, Program* program) {
    /* 'import' was already consumed by the top-level dispatcher */
    if (!check(parser, TOKEN_STRING)) {
        error_at_current(parser, "expected module path string");
        return;
    }
    int path_line = parser->current.line;
    int path_column = parser->current.column;
    char* raw_path = copy_token_lexeme(&parser->current);
    Stmt* stmt = create_import_stmt(raw_path);
    free(raw_path);
    if (stmt == NULL) {
        error_at_current(parser, "out of memory");
        return;
    }
    stmt->loc.line = path_line;
    stmt->loc.column = path_column;
    char* path = stmt->as.import_stmt.path;
    int len = (int)strlen(path);
    if (len >= 2 && path[0] == '"' && path[len - 1] == '"') {
        memmove(path, path + 1, (size_t)(len - 2));
        path[len - 2] = '\0';
    }
    advance(parser); /* string */
    if (!match(parser, TOKEN_SEMICOLON)) {
        error_at_current(parser, "expected ';' after import path");
        free_stmt(stmt);
        return;
    }
    if (!program_add_import(program, stmt)) {
        error_at_current(parser, "too many imports");
    }
}

static Token peek_next(Parser* parser) {
    Lexer copy = parser->lexer;
    return lexer_next_token(&copy);
}

static Stmt* statement(Parser* parser) {
    if (check(parser, TOKEN_INT_TYPE) ||
        check(parser, TOKEN_FLOAT_TYPE) ||
        check(parser, TOKEN_STRING_TYPE) ||
        check(parser, TOKEN_BOOL_TYPE) ||
        check(parser, TOKEN_ARRAY_TYPE) ||
        check(parser, TOKEN_MAP_TYPE)) {
        return var_decl(parser);
    }
    if (check(parser, TOKEN_IDENT)) {
        /* Ambiguity between struct-typed variable declarations
           (Type name identifier followed by variable name identifier)
           and other identifier-started statements. */
        if (peek_next(parser).type == TOKEN_IDENT) {
            return var_decl(parser);
        }
        return assignment(parser);
    }
    if (match(parser, TOKEN_IF)) return if_statement(parser);
    if (match(parser, TOKEN_WHILE)) return while_statement(parser);
    if (match(parser, TOKEN_DO)) return do_while_statement(parser);
    if (match(parser, TOKEN_FOR)) return for_statement(parser);
    if (match(parser, TOKEN_BREAK)) return break_statement(parser);
    if (match(parser, TOKEN_CONTINUE)) return continue_statement(parser);
    if (match(parser, TOKEN_RETURN)) return return_statement(parser);
    if (match(parser, TOKEN_PRINT)) return print_statement(parser);
    if (match(parser, TOKEN_CREATE) || match(parser, TOKEN_DROP)) {
        return sql_statement(parser, STMT_SQL_DDL);
    }
    if (match(parser, TOKEN_INSERT) || match(parser, TOKEN_UPDATE) || match(parser, TOKEN_DELETE)) {
        return sql_statement(parser, STMT_SQL_DML);
    }
    if (match(parser, TOKEN_BEGIN)) {
        advance(parser); /* consume ; */
        return create_sql_transaction_stmt(0);
    }
    if (match(parser, TOKEN_COMMIT)) {
        advance(parser); /* consume ; */
        return create_sql_transaction_stmt(1);
    }
    if (match(parser, TOKEN_ROLLBACK)) {
        advance(parser); /* consume ; */
        return create_sql_transaction_stmt(2);
    }
    if (match(parser, TOKEN_SQL_QUERY)) {
        return sql_statement(parser, STMT_SQL_QUERY);
    }
    error_at_current(parser, "expected statement");
    return NULL;
}

static void parse_proc(Parser* parser, Program* program) {
    /* current is IDENT (procedure name); 'proc' was consumed by caller */
    advance(parser); /* name */
    char* name = copy_token_lexeme(&parser->previous);
    advance(parser); /* ( */

    Param* params = NULL;
    int param_count = 0;
    if (!check(parser, TOKEN_RPAREN)) {
        do {
            advance(parser); /* param name */
            char* param_name = copy_token_lexeme(&parser->previous);
            Type* param_type = parse_type(parser);

            Param* new_params = realloc(params,
                sizeof(Param) * (size_t)(param_count + 1));
            if (new_params == NULL) {
                free(param_name);
                type_free(param_type);
                for (int i = 0; i < param_count; i++) {
                    free(params[i].name);
                    type_free(params[i].type);
                }
                free(params);
                free(name);
                error_at_current(parser, "out of memory");
                return;
            }
            params = new_params;
            params[param_count].name = param_name;
            params[param_count].type = param_type;
            param_count++;
        } while (match(parser, TOKEN_COMMA));
    }

    advance(parser); /* ) */
    advance(parser); /* -> */
    Type* return_type = parse_type(parser);
    ProcDecl* proc = create_proc_decl(name, return_type);
    free(name);
    if (proc == NULL) {
        for (int i = 0; i < param_count; i++) {
            free(params[i].name);
            type_free(params[i].type);
        }
        free(params);
        error_at_current(parser, "out of memory");
        return;
    }
    proc->params = params;
    proc->param_count = param_count;
    proc->body = block(parser);
    if (proc->body == NULL) {
        free(proc->name);
        for (int i = 0; i < param_count; i++) {
            free(proc->params[i].name);
            type_free(proc->params[i].type);
        }
        free(proc->params);
        type_free(proc->return_type);
        free(proc);
        return;
    }

    ProcDecl* new_procs = realloc(program->procs,
        sizeof(ProcDecl) * (size_t)(program->proc_count + 1));
    program->procs = new_procs;
    program->procs[program->proc_count++] = *proc;
    free(proc);
}

static ParseRule rules[] = {
    [TOKEN_PROC]       = {NULL,        NULL,   PREC_NONE},
    [TOKEN_FOR]        = {NULL,        NULL,   PREC_NONE},
    [TOKEN_IF]         = {NULL,        NULL,   PREC_NONE},
    [TOKEN_RETURN]     = {NULL,        NULL,   PREC_NONE},
    [TOKEN_IN]         = {NULL,        NULL,   PREC_NONE},
    [TOKEN_PRINT]      = {NULL,        NULL,   PREC_NONE},
    [TOKEN_IMPORT]     = {NULL,        NULL,   PREC_NONE},
    [TOKEN_INT_TYPE]   = {NULL,        NULL,   PREC_NONE},
    [TOKEN_FLOAT_TYPE] = {NULL,        NULL,   PREC_NONE},
    [TOKEN_STRING_TYPE]= {NULL,        NULL,   PREC_NONE},
    [TOKEN_BOOL_TYPE]  = {NULL,        NULL,   PREC_NONE},
    [TOKEN_ARRAY_TYPE] = {NULL,        NULL,   PREC_NONE},
    [TOKEN_TRUE]       = {literal_bool,NULL,   PREC_NONE},
    [TOKEN_FALSE]      = {literal_bool,NULL,   PREC_NONE},
    [TOKEN_IDENT]      = {variable,    NULL,   PREC_NONE},
    [TOKEN_INT]        = {number,      NULL,   PREC_NONE},
    [TOKEN_FLOAT]      = {number,      NULL,   PREC_NONE},
    [TOKEN_STRING]     = {number,      NULL,   PREC_NONE},
    [TOKEN_SQL_QUERY]  = {NULL,        NULL,   PREC_NONE},

    [TOKEN_ARROW]      = {NULL,        NULL,   PREC_NONE},
    [TOKEN_EQ]         = {NULL,        binary, PREC_EQUALITY},
    [TOKEN_NE]         = {NULL,        binary, PREC_EQUALITY},
    [TOKEN_GE]         = {NULL,        binary, PREC_COMPARISON},
    [TOKEN_LE]         = {NULL,        binary, PREC_COMPARISON},
    [TOKEN_ASSIGN]     = {NULL,        NULL,   PREC_NONE},
    [TOKEN_LT]         = {NULL,        binary, PREC_COMPARISON},
    [TOKEN_GT]         = {NULL,        binary, PREC_COMPARISON},
    [TOKEN_BANG]       = {unary,       NULL,   PREC_NONE},

    [TOKEN_PLUS]       = {NULL,        binary, PREC_TERM},
    [TOKEN_MINUS]      = {unary,       binary, PREC_TERM},
    [TOKEN_STAR]       = {NULL,        binary, PREC_FACTOR},
    [TOKEN_SLASH]      = {NULL,        binary, PREC_FACTOR},
    [TOKEN_DOT]        = {NULL,        field,  PREC_CALL},

    [TOKEN_LPAREN]     = {grouping,    call,   PREC_CALL},
    [TOKEN_RPAREN]     = {NULL,        NULL,   PREC_NONE},
    [TOKEN_LBRACE]     = {map_literal, NULL,   PREC_NONE},
    [TOKEN_RBRACE]     = {NULL,        NULL,   PREC_NONE},
    [TOKEN_LBRACKET]   = {array_literal, index_expr, PREC_CALL},
    [TOKEN_RBRACKET]   = {NULL,        NULL,   PREC_NONE},
    [TOKEN_COMMA]      = {NULL,        NULL,   PREC_NONE},
    [TOKEN_SEMICOLON]  = {NULL,        NULL,   PREC_NONE},
    [TOKEN_COLON]      = {NULL,        NULL,   PREC_NONE},

    [TOKEN_ERROR]      = {NULL,        NULL,   PREC_NONE},
    [TOKEN_EOF]        = {NULL,        NULL,   PREC_NONE},
};

static ParseRule* get_rule(TokenType type) {
    return &rules[type];
}

Expr* parse_expression(const char* source) {
    Parser parser;
    lexer_init(&parser.lexer, source);
    parser.had_error = 0;
    parser.path = NULL;
    parser.struct_count = 0;
    advance(&parser);
    Expr* expr = expression(&parser);
    parser_free_struct_names(&parser);
    if (parser.had_error) {
        free_expr(expr);
        return NULL;
    }
    return expr;
}

static void parse_struct(Parser* parser, Program* program) {
    /* 'struct' was already consumed by the top-level dispatcher */
    if (!check(parser, TOKEN_IDENT)) {
        error_at_current(parser, "expected struct name");
        return;
    }
    advance(parser); /* name */
    char* name = copy_token_lexeme(&parser->previous);

    if (!match(parser, TOKEN_LBRACE)) {
        error_at_current(parser, "expected '{' after struct name");
        free(name);
        return;
    }

    char** field_names = NULL;
    Type** field_types = NULL;
    int field_count = 0;
    while (!check(parser, TOKEN_RBRACE) && !check(parser, TOKEN_EOF) && !parser->had_error) {
        if (!check(parser, TOKEN_IDENT)) {
            error_at_current(parser, "expected field name");
            break;
        }
        advance(parser); /* field name */
        char** new_names = realloc(field_names, sizeof(char*) * (size_t)(field_count + 1));
        Type** new_types = realloc(field_types, sizeof(Type*) * (size_t)(field_count + 1));
        if (new_names == NULL || new_types == NULL) {
            error_at_current(parser, "out of memory");
            break;
        }
        field_names = new_names;
        field_types = new_types;
        field_names[field_count] = copy_token_lexeme(&parser->previous);
        field_types[field_count] = parse_type(parser);
        field_count++;
        if (!check(parser, TOKEN_RBRACE)) {
            if (!match(parser, TOKEN_COMMA)) {
                error_at_current(parser, "expected ',' or '}' in struct declaration");
                break;
            }
        }
    }

    if (parser->had_error) {
        for (int i = 0; i < field_count; i++) {
            free(field_names[i]);
            type_free(field_types[i]);
        }
        free(field_names);
        free(field_types);
        free(name);
        return;
    }

    if (!match(parser, TOKEN_RBRACE)) {
        error_at_current(parser, "expected '}' after struct fields");
        for (int i = 0; i < field_count; i++) {
            free(field_names[i]);
            type_free(field_types[i]);
        }
        free(field_names);
        free(field_types);
        free(name);
        return;
    }

    if (program->struct_count >= program->struct_capacity) {
        int new_capacity = program->struct_capacity == 0 ? 4 : program->struct_capacity * 2;
        StructDecl* new_structs = realloc(program->structs,
            sizeof(StructDecl) * (size_t)new_capacity);
        if (new_structs == NULL) {
            error_at_current(parser, "out of memory");
            for (int i = 0; i < field_count; i++) {
                free(field_names[i]);
                type_free(field_types[i]);
            }
            free(field_names);
            free(field_types);
            free(name);
            return;
        }
        program->structs = new_structs;
        program->struct_capacity = new_capacity;
    }

    StructDecl* decl = &program->structs[program->struct_count++];
    decl->name = name;
    decl->field_names = field_names;
    decl->field_types = field_types;
    decl->field_count = field_count;
    parser_add_struct_name(parser, name);
}

Program* parse_with_path(const char* source, const char* path, char* error, size_t error_size) {
    Parser parser;
    lexer_init(&parser.lexer, source);
    parser.had_error = 0;
    parser.path = path;
    parser.error_message[0] = '\0';
    parser.struct_count = 0;
    advance(&parser);

    Program* program = create_program();
    while (!check(&parser, TOKEN_EOF) && !parser.had_error) {
        if (match(&parser, TOKEN_IMPORT)) {
            parse_import(&parser, program);
        } else if (match(&parser, TOKEN_STRUCT)) {
            parse_struct(&parser, program);
        } else if (match(&parser, TOKEN_PROC)) {
            parse_proc(&parser, program);
        } else {
            error_at_current(&parser, "expected 'proc', 'struct', or 'import'");
            break;
        }
    }

    if (parser.had_error) {
        if (error != NULL && error_size > 0) {
            strncpy(error, parser.error_message, error_size - 1);
            error[error_size - 1] = '\0';
        }
        parser_free_struct_names(&parser);
        free_program(program);
        return NULL;
    }
    parser_free_struct_names(&parser);
    return program;
}

Program* parse(const char* source, char* error, size_t error_size) {
    return parse_with_path(source, NULL, error, error_size);
}
