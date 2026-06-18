#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"
#include "lexer.h"

typedef struct {
    Lexer lexer;
    Token current;
    Token previous;
    int had_error;
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
    fprintf(stderr, "Parse error at line %d: %s (got token %d)\n",
            parser->current.line, message, parser->current.type);
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

static Expr* grouping(Parser* parser) {
    Expr* expr = expression(parser);
    advance(parser); /* consume ) */
    return expr;
}

static Expr* unary(Parser* parser) {
    TokenType op = parser->previous.type;
    Expr* operand = parse_precedence(parser, PREC_UNARY);
    return create_unary_expr(op, operand);
}

static Expr* number(Parser* parser) {
    if (parser->previous.type == TOKEN_FLOAT) {
        /* TODO: parse float lexeme */
        return create_literal_expr(value_float(0.0));
    }
    int value = 0;
    for (int i = 0; i < parser->previous.length; i++) {
        value = value * 10 + (parser->previous.start[i] - '0');
    }
    return create_literal_expr(value_int(value));
}

static Expr* variable(Parser* parser) {
    return create_variable_expr(copy_token_lexeme(&parser->previous));
}

static Expr* field(Parser* parser, Expr* left) {
    advance(parser); /* field name */
    char* field_name = copy_token_lexeme(&parser->previous);
    Expr* expr = create_field_expr(left->as.variable.name, field_name);
    free(field_name);
    free_expr(left);
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
    free_expr(left);
    return expr;
}

static Expr* binary(Parser* parser, Expr* left) {
    TokenType op = parser->previous.type;
    ParseRule* rule = get_rule(op);
    Expr* right = parse_precedence(parser, (Precedence)(rule->precedence + 1));
    return create_binary_expr(op, left, right);
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

static TypeKind parse_type(Parser* parser) {
    if (match(parser, TOKEN_INT_TYPE)) return TYPE_INT;
    if (match(parser, TOKEN_FLOAT_TYPE)) return TYPE_FLOAT;
    if (match(parser, TOKEN_STRING)) return TYPE_STRING;
    error_at_current(parser, "expected type");
    return TYPE_INT;
}

static Stmt* var_decl(Parser* parser);
static Stmt* statement(Parser* parser);

static Block* block(Parser* parser) {
    Block* result = create_block();
    advance(parser); /* { */
    while (!check(parser, TOKEN_RBRACE) && !check(parser, TOKEN_EOF)) {
        Stmt** new_stmts = realloc(result->stmts,
            sizeof(Stmt*) * (size_t)(result->stmt_count + 1));
        result->stmts = new_stmts;
        result->stmts[result->stmt_count++] = statement(parser);
    }
    advance(parser); /* } */
    return result;
}

static Stmt* var_decl(Parser* parser) {
    TypeKind type = parse_type(parser);
    advance(parser); /* identifier */
    char* name = copy_token_lexeme(&parser->previous);
    Expr* init = NULL;
    if (match(parser, TOKEN_ASSIGN)) {
        init = expression(parser);
    }
    advance(parser); /* semicolon */
    Stmt* stmt = create_var_decl_stmt(type, name, init);
    free(name);
    return stmt;
}

static Stmt* assignment(Parser* parser) {
    advance(parser); /* identifier */
    char* name = copy_token_lexeme(&parser->previous);
    advance(parser); /* = */
    Expr* value = expression(parser);
    advance(parser); /* ; */
    Stmt* stmt = create_assign_stmt(name, value);
    free(name);
    return stmt;
}

static Stmt* if_statement(Parser* parser) {
    /* 'if' was already consumed by the statement dispatcher */
    Expr* cond = expression(parser);
    Block* then_block = block(parser);
    Block* else_block = NULL;
    if (match(parser, TOKEN_ELSE)) {
        else_block = block(parser);
    }
    return create_if_stmt(cond, then_block, else_block);
}

static Stmt* for_statement(Parser* parser) {
    /* 'for' was already consumed by the statement dispatcher */
    advance(parser); /* iterator name */
    char* var_name = copy_token_lexeme(&parser->previous);
    advance(parser); /* in */
    advance(parser); /* SQL query */
    char* sql_query = copy_token_lexeme_trimmed(&parser->previous);
    Block* body = block(parser);
    Stmt* stmt = create_for_stmt(var_name, sql_query, body);
    free(var_name);
    free(sql_query);
    return stmt;
}

static Stmt* return_statement(Parser* parser) {
    /* 'return' was already consumed by the statement dispatcher */
    Expr* value = expression(parser);
    advance(parser); /* ; */
    return create_return_stmt(value);
}

static Stmt* statement(Parser* parser) {
    if (check(parser, TOKEN_INT_TYPE) || check(parser, TOKEN_FLOAT_TYPE)) {
        return var_decl(parser);
    }
    if (match(parser, TOKEN_IF)) return if_statement(parser);
    if (match(parser, TOKEN_FOR)) return for_statement(parser);
    if (match(parser, TOKEN_RETURN)) return return_statement(parser);
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
            TypeKind param_type = parse_type(parser);

            Param* new_params = realloc(params,
                sizeof(Param) * (size_t)(param_count + 1));
            if (new_params == NULL) {
                free(param_name);
                for (int i = 0; i < param_count; i++) {
                    free(params[i].name);
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
    TypeKind return_type = parse_type(parser);
    ProcDecl* proc = create_proc_decl(name, return_type);
    free(name);
    if (proc == NULL) {
        for (int i = 0; i < param_count; i++) {
            free(params[i].name);
        }
        free(params);
        parser->had_error = 1;
        return;
    }
    proc->params = params;
    proc->param_count = param_count;
    proc->body = block(parser);

    ProcDecl* new_procs = realloc(program->procs,
        sizeof(ProcDecl) * (size_t)(program->proc_count + 1));
    program->procs = new_procs;
    program->procs[program->proc_count++] = *proc;
    free(proc);
}

static ParseRule rules[] = {
    [TOKEN_PROC]       = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FOR]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_IF]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_RETURN]     = {NULL,     NULL,   PREC_NONE},
    [TOKEN_IN]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_INT_TYPE]   = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FLOAT_TYPE] = {NULL,     NULL,   PREC_NONE},
    [TOKEN_IDENT]      = {variable, NULL,   PREC_NONE},
    [TOKEN_INT]        = {number,   NULL,   PREC_NONE},
    [TOKEN_FLOAT]      = {number,   NULL,   PREC_NONE},
    [TOKEN_STRING]     = {number,   NULL,   PREC_NONE},
    [TOKEN_SQL_QUERY]  = {NULL,     NULL,   PREC_NONE},

    [TOKEN_ARROW]      = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EQ]         = {NULL,     binary, PREC_EQUALITY},
    [TOKEN_NE]         = {NULL,     binary, PREC_EQUALITY},
    [TOKEN_GE]         = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_LE]         = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_ASSIGN]     = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LT]         = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_GT]         = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_BANG]       = {unary,    NULL,   PREC_NONE},

    [TOKEN_PLUS]       = {NULL,     binary, PREC_TERM},
    [TOKEN_MINUS]      = {unary,    binary, PREC_TERM},
    [TOKEN_STAR]       = {NULL,     binary, PREC_FACTOR},
    [TOKEN_SLASH]      = {NULL,     binary, PREC_FACTOR},
    [TOKEN_DOT]        = {NULL,     field,  PREC_CALL},

    [TOKEN_LPAREN]     = {grouping, call,   PREC_CALL},
    [TOKEN_RPAREN]     = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LBRACE]     = {NULL,     NULL,   PREC_NONE},
    [TOKEN_RBRACE]     = {NULL,     NULL,   PREC_NONE},
    [TOKEN_COMMA]      = {NULL,     NULL,   PREC_NONE},
    [TOKEN_SEMICOLON]  = {NULL,     NULL,   PREC_NONE},
    [TOKEN_COLON]      = {NULL,     NULL,   PREC_NONE},

    [TOKEN_ERROR]      = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EOF]        = {NULL,     NULL,   PREC_NONE},
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

Program* parse(const char* source) {
    Parser parser;
    lexer_init(&parser.lexer, source);
    parser.had_error = 0;
    advance(&parser);

    Program* program = create_program();
    while (!check(&parser, TOKEN_EOF)) {
        if (match(&parser, TOKEN_PROC)) {
            parse_proc(&parser, program);
        } else {
            error_at_current(&parser, "expected 'proc'");
            break;
        }
    }

    if (parser.had_error) {
        free_program(program);
        return NULL;
    }
    return program;
}
