#ifndef MYDB_AST_H
#define MYDB_AST_H

#include "lexer.h"
#include "compiler.h"

typedef enum {
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_STRING
} TypeKind;

typedef enum {
    EXPR_LITERAL,
    EXPR_VARIABLE,
    EXPR_BINARY,
    EXPR_UNARY,
    EXPR_FIELD
} ExprKind;

typedef enum {
    STMT_VAR_DECL,
    STMT_ASSIGN,
    STMT_IF,
    STMT_FOR,
    STMT_RETURN
} StmtKind;

typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Block Block;

typedef struct {
    char* name;
    TypeKind type;
} Param;

typedef struct {
    char* name;
    Param* params;
    int param_count;
    TypeKind return_type;
    Block* body;
} ProcDecl;

typedef struct {
    ProcDecl* procs;
    int proc_count;
} Program;

struct Block {
    Stmt** stmts;
    int stmt_count;
};

typedef struct {
    TypeKind type;
    char* name;
    Expr* initializer;
} VarDeclStmt;

typedef struct {
    char* name;
    Expr* value;
} AssignStmt;

typedef struct {
    Expr* condition;
    Block* then_block;
    Block* else_block;
} IfStmt;

typedef struct {
    char* var_name;
    char* sql_query;
    Block* body;
} ForStmt;

typedef struct {
    Expr* value;
} ReturnStmt;

struct Stmt {
    StmtKind kind;
    union {
        VarDeclStmt var_decl;
        AssignStmt assign;
        IfStmt if_stmt;
        ForStmt for_stmt;
        ReturnStmt return_stmt;
    } as;
};

typedef struct {
    Value value;
} LiteralExpr;

typedef struct {
    char* name;
} VariableExpr;

typedef struct {
    TokenType op;
    Expr* left;
    Expr* right;
} BinaryExpr;

typedef struct {
    TokenType op;
    Expr* operand;
} UnaryExpr;

typedef struct {
    char* row;
    char* field;
} FieldExpr;

struct Expr {
    ExprKind kind;
    union {
        LiteralExpr literal;
        VariableExpr variable;
        BinaryExpr binary;
        UnaryExpr unary;
        FieldExpr field;
    } as;
};

Program* create_program(void);
void free_program(Program* program);

ProcDecl* create_proc_decl(const char* name, TypeKind return_type);

Block* create_block(void);

Stmt* create_var_decl_stmt(TypeKind type, const char* name, Expr* init);
Stmt* create_assign_stmt(const char* name, Expr* value);
Stmt* create_if_stmt(Expr* cond, Block* then_block, Block* else_block);
Stmt* create_for_stmt(const char* var_name, const char* sql_query, Block* body);
Stmt* create_return_stmt(Expr* value);

Expr* create_literal_expr(Value value);
Expr* create_variable_expr(const char* name);
Expr* create_binary_expr(TokenType op, Expr* left, Expr* right);
Expr* create_unary_expr(TokenType op, Expr* operand);
Expr* create_field_expr(const char* row, const char* field);

#endif
