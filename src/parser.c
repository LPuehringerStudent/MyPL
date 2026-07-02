#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"
#include "lexer.h"

#define MAX_IMPORTS 64

typedef struct {
    Lexer lexer;
    Token current;
    Token previous;
    int had_error;
    char error_message[256];
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
    snprintf(parser->error_message, sizeof(parser->error_message),
             "Parse error at line %d:%d: %s (got token %d)",
             parser->current.line, parser->current.column, message,
             parser->current.type);
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

static char* copy_token_lexeme_trimmed(const Token* token) {
    int length = token->length;
    while (length > 0) {
        char c = token->start[length - 1];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
        length--;
    }
    char* lexeme = malloc((size_t)length + 1);
    memcpy(lexeme, token->start, (size_t)length);
    lexeme[length] = '\0';
    return lexeme;
}

static Expr* expression(Parser* parser);

static int is_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static Stmt* sql_statement(Parser* parser, int kind) {
    const char* sql_start = parser->previous.start;
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

    Stmt* stmt = create_sql_stmt(kind, sql, params, param_count);
    free(sql);
    if (stmt == NULL) {
        for (int i = 0; i < param_count; i++) free_expr(params[i]);
        free(params);
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
        char* str = malloc((size_t)len + 1);
        if (str == NULL) return create_literal_expr(value_int(0));
        memcpy(str, parser->previous.start + 1, (size_t)len);
        str[len] = '\0';
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

static Expr* variable(Parser* parser) {
    Expr* expr = create_variable_expr(copy_token_lexeme(&parser->previous));
    expr->loc.line = parser->previous.line;
    expr->loc.column = parser->previous.column;
    return expr;
}

static Expr* field(Parser* parser, Expr* left) {
    advance(parser); /* field name */
    char* field_name = copy_token_lexeme(&parser->previous);
    Expr* expr;
    if (left->kind == EXPR_VARIABLE) {
        expr = create_field_expr(left->as.variable.name, field_name);
    } else {
        expr = create_row_field_expr(left, field_name);
    }
    expr->loc.line = parser->previous.line;
    expr->loc.column = parser->previous.column;
    free(field_name);
    if (left->kind == EXPR_VARIABLE) {
        free_expr(left);
    }
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
        error_at_current(parser, "expected expression");
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
        else_block = block(parser);
        if (else_block == NULL) {
            free_expr(cond);
            free_block(then_block);
            return NULL;
        }
    }
    Stmt* stmt = create_if_stmt(cond, then_block, else_block);
    stmt->loc.line = kw.line;
    stmt->loc.column = kw.column;
    return stmt;
}

static Stmt* for_statement(Parser* parser) {
    Token kw = parser->previous;  /* 'for' was just consumed */
    advance(parser); /* iterator name */
    char* var_name = copy_token_lexeme(&parser->previous);
    advance(parser); /* in */
    advance(parser); /* SQL query */
    char* sql_query = copy_token_lexeme_trimmed(&parser->previous);
    Block* body = block(parser);
    if (body == NULL) {
        free(var_name);
        free(sql_query);
        return NULL;
    }
    Stmt* stmt = create_for_stmt(var_name, sql_query, body);
    stmt->loc.line = kw.line;
    stmt->loc.column = kw.column;
    free(var_name);
    free(sql_query);
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

static Stmt* statement(Parser* parser) {
    if (check(parser, TOKEN_INT_TYPE) ||
        check(parser, TOKEN_FLOAT_TYPE) ||
        check(parser, TOKEN_STRING_TYPE) ||
        check(parser, TOKEN_BOOL_TYPE) ||
        check(parser, TOKEN_ARRAY_TYPE)) {
        return var_decl(parser);
    }
    if (match(parser, TOKEN_IF)) return if_statement(parser);
    if (match(parser, TOKEN_FOR)) return for_statement(parser);
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
    if (check(parser, TOKEN_IDENT)) return assignment(parser);
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
                parser->had_error = 1;
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
        parser->had_error = 1;
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
    [TOKEN_LBRACE]     = {NULL,        NULL,   PREC_NONE},
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
    advance(&parser);
    Expr* expr = expression(&parser);
    if (parser.had_error) {
        return NULL;
    }
    return expr;
}

Program* parse(const char* source, char* error, size_t error_size) {
    Parser parser;
    lexer_init(&parser.lexer, source);
    parser.had_error = 0;
    parser.error_message[0] = '\0';
    advance(&parser);

    Program* program = create_program();
    while (!check(&parser, TOKEN_EOF) && !parser.had_error) {
        if (match(&parser, TOKEN_IMPORT)) {
            parse_import(&parser, program);
        } else if (match(&parser, TOKEN_PROC)) {
            parse_proc(&parser, program);
        } else {
            error_at_current(&parser, "expected 'proc' or 'import'");
            break;
        }
    }

    if (parser.had_error) {
        if (error != NULL && error_size > 0) {
            strncpy(error, parser.error_message, error_size - 1);
            error[error_size - 1] = '\0';
        }
        free_program(program);
        return NULL;
    }
    return program;
}
