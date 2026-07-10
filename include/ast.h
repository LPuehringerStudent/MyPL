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
    TYPE_MAP,
    TYPE_ROW,
    TYPE_CURSOR,
    TYPE_STRUCT,
    TYPE_UNKNOWN
} TypeKind;

typedef struct Type {
    TypeKind kind;
    struct Type* element_type;  /* only when kind == TYPE_ARRAY or TYPE_MAP (value type for maps) */
    struct Type* map_key_type;  /* only when kind == TYPE_MAP */
    /* only when kind == TYPE_STRUCT */
    char* struct_name;
    char** field_names;
    struct Type** field_types;
    int field_count;
} Type;

extern Type type_int;
extern Type type_float;
extern Type type_string;
extern Type type_bool;
extern Type type_row;
extern Type type_cursor;
extern Type type_unknown;

Type* type_new(TypeKind kind, Type* element_type);
Type* type_new_map(Type* key_type, Type* value_type);
Type* type_copy(Type* type);
void  type_free(Type* type);
int   type_equals(Type* a, Type* b);
int   type_is_numeric(Type* t);
int   type_is_array(Type* t);
int   type_is_map(Type* t);
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
    EXPR_ROW_FIELD,
    EXPR_STRUCT_LITERAL,
    EXPR_MAP_LITERAL,
    EXPR_CURSOR_ATTR
} ExprKind;

typedef enum {
    STMT_VAR_DECL,
    STMT_ASSIGN,
    STMT_IF,
    STMT_FOR,
    STMT_FOREACH,
    STMT_WHILE,
    STMT_BREAK,
    STMT_CONTINUE,
    STMT_RETURN,
    STMT_PRINT,
    STMT_INDEX_ASSIGN,
    STMT_EXPR,
    STMT_FOR_C,
    STMT_IMPORT,
    STMT_SQL_DDL,
    STMT_SQL_DML,
    STMT_SQL_QUERY,
    STMT_SQL_TRANSACTION,
    STMT_TRY_CATCH,
    STMT_CASE,
    STMT_CURSOR_DECL,
    STMT_CURSOR_OPEN,
    STMT_CURSOR_FETCH,
    STMT_CURSOR_CLOSE,
    STMT_EXCEPTION_DECL,
    STMT_RAISE
} StmtKind;

typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Block Block;

typedef struct {
    int line;
    int column;
} SourceLoc;

typedef enum {
    PARAM_IN,
    PARAM_OUT,
    PARAM_INOUT
} ParamMode;

typedef struct {
    char* name;
    Type* type;
    ParamMode mode;
} Param;

typedef struct {
    char* name;
    Param* params;
    int param_count;
    Type* return_type;
    Block* body;
    int is_function;  /* 1 = function (may be used in SQL expressions), 0 = procedure */
} ProcDecl;

typedef struct {
    char* path;
} ImportStmt;

typedef struct {
    char* name;
    char** field_names;
    Type** field_types;
    int field_count;
} StructDecl;

typedef struct {
    char* name;
    Stmt** vars;
    int var_count;
    int var_capacity;
    ProcDecl* procs;
    int proc_count;
    int proc_capacity;
    ProcDecl* funcs;
    int func_count;
    int func_capacity;
} PackageSpecDecl;

typedef struct {
    char* name;
    Stmt** vars;
    int var_count;
    int var_capacity;
    ProcDecl* procs;
    int proc_count;
    int proc_capacity;
    ProcDecl* funcs;
    int func_count;
    int func_capacity;
} PackageBodyDecl;

typedef struct {
    Stmt** imports;
    int import_count;
    int import_capacity;
    StructDecl* structs;
    int struct_count;
    int struct_capacity;
    PackageSpecDecl* specs;
    int spec_count;
    int spec_capacity;
    PackageBodyDecl* bodies;
    int body_count;
    int body_capacity;
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
    Expr* selector;
    Expr** values;
    Block** blocks;
    int branch_count;
    Block* else_block;
} CaseStmt;

typedef struct {
    char* var_name;
    char* sql_query;
    Expr** params;
    int param_count;
    Block* body;
} ForStmt;

typedef struct {
    char* var_name;
    Expr* iterable;
    Block* body;
} ForeachStmt;

typedef struct {
    Expr* condition;
    Block* body;
    int is_do_while;
} WhileStmt;

typedef struct {
    Stmt* init;
    Expr* condition;
    Stmt* step;
    Block* body;
} CForStmt;

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

typedef struct {
    char* name;
    char* sql_query;  /* static query for declarations, NULL for uninitialized */
} CursorDeclStmt;

typedef struct {
    char* name;
    char* sql_query;  /* NULL for static open (uses declaration query) */
    Expr** params;
    int param_count;
} CursorOpenStmt;

typedef struct {
    char* name;
    char** into_vars;
    int into_count;
} CursorFetchStmt;

typedef struct {
    char* name;
} CursorCloseStmt;

typedef struct {
    char* name;
} ExceptionDeclStmt;

typedef struct {
    char* name;
} RaiseStmt;

typedef struct {
    Block* try_block;
    char* catch_var;
    Block* catch_block;
} TryCatchStmt;

struct Stmt {
    StmtKind kind;
    SourceLoc loc;
    union {
        VarDeclStmt var_decl;
        AssignStmt assign;
        IfStmt if_stmt;
        ForStmt for_stmt;
        ForeachStmt foreach_stmt;
        WhileStmt while_stmt;
        ReturnStmt return_stmt;
        PrintStmt print_stmt;
        IndexAssignStmt index_assign;
        ExprStmt expr_stmt;
        CForStmt cfor_stmt;
        ImportStmt import_stmt;
        SqlStmt sql_stmt;
        SqlTransactionStmt sql_transaction;
        TryCatchStmt try_catch;
        CaseStmt case_stmt;
        CursorDeclStmt cursor_decl;
        CursorOpenStmt cursor_open;
        CursorFetchStmt cursor_fetch;
        CursorCloseStmt cursor_close;
        ExceptionDeclStmt exception_decl;
        RaiseStmt raise_stmt;
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
    char* package_name;  /* non-NULL for qualified calls pkg.name(...) */
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

typedef struct {
    char* struct_name;
    char** field_names;
    Expr** values;
    int field_count;
} StructLiteralExpr;

typedef struct {
    Expr** keys;
    Expr** values;
    int count;
} MapLiteralExpr;

typedef struct {
    char* cursor_name;
    char* attr_name;
} CursorAttrExpr;

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
        StructLiteralExpr struct_literal;
        MapLiteralExpr map_literal;
        CursorAttrExpr cursor_attr;
    } as;
};

Program* create_program(void);
void free_program(Program* program);

void free_block(Block* block);

void free_expr(Expr* expr);
void free_stmt(Stmt* stmt);
void free_package_spec(PackageSpecDecl* spec);
void free_package_body(PackageBodyDecl* body);

ProcDecl* create_proc_decl(const char* name, Type* return_type);

Block* create_block(void);

Stmt* create_var_decl_stmt(Type* type, const char* name, Expr* init);
Stmt* create_assign_stmt(const char* name, Expr* value);
Stmt* create_if_stmt(Expr* cond, Block* then_block, Block* else_block);
Stmt* create_for_stmt(const char* var_name, const char* sql_query, Expr** params, int param_count, Block* body);
Stmt* create_foreach_stmt(const char* var_name, Expr* iterable, Block* body);
Stmt* create_while_stmt(Expr* condition, Block* body);
Stmt* create_do_while_stmt(Expr* condition, Block* body);
Stmt* create_break_stmt(void);
Stmt* create_continue_stmt(void);
Stmt* create_return_stmt(Expr* value);
Stmt* create_print_stmt(Expr* value);
Stmt* create_expr_stmt(Expr* value);
Stmt* create_cfor_stmt(Stmt* init, Expr* condition, Stmt* step, Block* body);
Stmt* create_import_stmt(const char* path);
Stmt* create_sql_stmt(int kind, char* sql, Expr** params, int param_count, char** into_vars, int into_count);
Stmt* create_sql_transaction_stmt(int kind);
Stmt* create_try_catch_stmt(Block* try_block, const char* catch_var, Block* catch_block);
Stmt* create_case_stmt(Expr* selector, Expr** values, Block** blocks, int branch_count, Block* else_block);
Stmt* create_cursor_decl_stmt(const char* name, char* sql_query);
Stmt* create_cursor_open_stmt(const char* name, char* sql_query, Expr** params, int param_count);
Stmt* create_cursor_fetch_stmt(const char* name, char** into_vars, int into_count);
Stmt* create_cursor_close_stmt(const char* name);
Stmt* create_exception_decl_stmt(const char* name);
Stmt* create_raise_stmt(const char* name);
Expr* create_cursor_attr_expr(const char* cursor_name, const char* attr_name);

Expr* create_literal_expr(Value value);
Expr* create_variable_expr(const char* name);
Expr* create_binary_expr(TokenType op, Expr* left, Expr* right);
Expr* create_unary_expr(TokenType op, Expr* operand);
Expr* create_field_expr(const char* row, const char* field);
Expr* create_call_expr(const char* name, Expr** args, int arg_count);
Expr* create_qualified_call_expr(const char* package_name, const char* name, Expr** args, int arg_count);
Expr* create_array_expr(Expr** elements, int count);
Expr* create_index_expr(Expr* array, Expr* index);
Expr* create_sql_param_expr(const char* name);
Expr* create_row_field_expr(Expr* row, const char* field);
Expr* create_struct_literal_expr(const char* struct_name, char** field_names, Expr** values, int field_count);
Expr* create_map_literal_expr(Expr** keys, Expr** values, int count);
Stmt* create_index_assign_stmt(Expr* array, Expr* index, Expr* value);

#endif
