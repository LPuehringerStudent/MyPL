#ifndef MYDB_AST_H
#define MYDB_AST_H

#include "lexer.h"
#include "compiler.h"

typedef enum {
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_STRING,
    TYPE_BOOL,
    TYPE_ARRAY,
    TYPE_ROW,
    TYPE_UNKNOWN
} TypeKind;

typedef struct Type {
    TypeKind kind;
    struct Type* element_type;  /* only when kind == TYPE_ARRAY */
} Type;

extern Type type_int;
extern Type type_float;
extern Type type_string;
extern Type type_bool;
extern Type type_row;
extern Type type_unknown;

Type* type_new(TypeKind kind, Type* element_type);
Type* type_copy(Type* type);
void  type_free(Type* type);
int   type_equals(Type* a, Type* b);
int   type_is_numeric(Type* t);
int   type_is_array(Type* t);
int   type_is_unknown(Type* t);
const char* type_name(Type* t);

typedef enum {
    EXPR_LITERAL,
    EXPR_VARIABLE,
    EXPR_BINARY,
    EXPR_UNARY,
    EXPR_FIELD,
    EXPR_CALL,
    EXPR_ARRAY,
    EXPR_INDEX,
    EXPR_SQL_PARAM,
    EXPR_ROW_FIELD
} ExprKind;

typedef enum {
    STMT_VAR_DECL,
    STMT_ASSIGN,
    STMT_IF,
    STMT_FOR,
    STMT_RETURN,
    STMT_PRINT,
    STMT_INDEX_ASSIGN,
    STMT_EXPR,
    STMT_IMPORT,
    STMT_SQL_DDL,
    STMT_SQL_DML,
    STMT_SQL_QUERY,
    STMT_SQL_TRANSACTION
} StmtKind;

typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Block Block;

typedef struct {
    int line;
    int column;
} SourceLoc;

typedef struct {
    char* name;
    Type* type;
} Param;

typedef struct {
    char* name;
    Param* params;
    int param_count;
    Type* return_type;
    Block* body;
} ProcDecl;

typedef struct {
    char* path;
} ImportStmt;

typedef struct {
    Stmt** imports;
    int import_count;
    int import_capacity;
    ProcDecl* procs;
    int proc_count;
} Program;

struct Block {
    Stmt** stmts;
    int stmt_count;
};

typedef struct {
    Type* type;
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
    Expr** params;
    int param_count;
    Block* body;
} ForStmt;

typedef struct {
    Expr* value;
} ReturnStmt;

typedef struct {
    Expr* value;
} PrintStmt;

typedef struct {
    Expr* array;
    Expr* index;
    Expr* value;
} IndexAssignStmt;

typedef struct {
    Expr* value;
} ExprStmt;

typedef struct {
    char* sql;
    Expr** params;
    int param_count;
    char** into_vars;
    int into_count;
} SqlStmt;

typedef struct {
    int kind; /* 0=begin, 1=commit, 2=rollback */
} SqlTransactionStmt;

struct Stmt {
    StmtKind kind;
    SourceLoc loc;
    union {
        VarDeclStmt var_decl;
        AssignStmt assign;
        IfStmt if_stmt;
        ForStmt for_stmt;
        ReturnStmt return_stmt;
        PrintStmt print_stmt;
        IndexAssignStmt index_assign;
        ExprStmt expr_stmt;
        ImportStmt import_stmt;
        SqlStmt sql_stmt;
        SqlTransactionStmt sql_transaction;
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

typedef struct {
    char* name;
    Expr** args;
    int arg_count;
} CallExpr;

typedef struct {
    Expr** elements;
    int count;
} ArrayExpr;

typedef struct {
    Expr* array;
    Expr* index;
} IndexExpr;

typedef struct {
    char* name;
} SqlParamExpr;

typedef struct {
    Expr* row;
    char* field;
} RowFieldExpr;

struct Expr {
    ExprKind kind;
    SourceLoc loc;
    union {
        LiteralExpr literal;
        VariableExpr variable;
        BinaryExpr binary;
        UnaryExpr unary;
        FieldExpr field;
        CallExpr call;
        ArrayExpr array;
        IndexExpr index;
        SqlParamExpr sql_param;
        RowFieldExpr row_field;
    } as;
};

Program* create_program(void);
void free_program(Program* program);

void free_block(Block* block);

void free_expr(Expr* expr);
void free_stmt(Stmt* stmt);

ProcDecl* create_proc_decl(const char* name, Type* return_type);

Block* create_block(void);

Stmt* create_var_decl_stmt(Type* type, const char* name, Expr* init);
Stmt* create_assign_stmt(const char* name, Expr* value);
Stmt* create_if_stmt(Expr* cond, Block* then_block, Block* else_block);
Stmt* create_for_stmt(const char* var_name, const char* sql_query, Expr** params, int param_count, Block* body);
Stmt* create_return_stmt(Expr* value);
Stmt* create_print_stmt(Expr* value);
Stmt* create_expr_stmt(Expr* value);
Stmt* create_import_stmt(const char* path);
Stmt* create_sql_stmt(int kind, char* sql, Expr** params, int param_count, char** into_vars, int into_count);
Stmt* create_sql_transaction_stmt(int kind);

Expr* create_literal_expr(Value value);
Expr* create_variable_expr(const char* name);
Expr* create_binary_expr(TokenType op, Expr* left, Expr* right);
Expr* create_unary_expr(TokenType op, Expr* operand);
Expr* create_field_expr(const char* row, const char* field);
Expr* create_call_expr(const char* name, Expr** args, int arg_count);
Expr* create_array_expr(Expr** elements, int count);
Expr* create_index_expr(Expr* array, Expr* index);
Expr* create_sql_param_expr(const char* name);
Expr* create_row_field_expr(Expr* row, const char* field);
Stmt* create_index_assign_stmt(Expr* array, Expr* index, Expr* value);

#endif
