#include <stdlib.h>
#include <string.h>

#include "compiler.h"
#include "parser.h"

#define MAX_LOCALS 256

typedef struct {
    const char* name;
    int length;
    int depth;
} Local;

typedef struct {
    Chunk* chunk;
    Local locals[MAX_LOCALS];
    int local_count;
    int scope_depth;
} Compiler;

static void emit_byte(Compiler* compiler, uint8_t byte) {
    write_chunk(compiler->chunk, byte);
}

static void emit_u16(Compiler* compiler, uint16_t value) {
    write_chunk_u16(compiler->chunk, value);
}

static int emit_jump(Compiler* compiler, uint8_t op) {
    emit_byte(compiler, op);
    emit_u16(compiler, 0);
    return compiler->chunk->count - 2;
}

static void patch_jump(Compiler* compiler, int offset) {
    int jump = compiler->chunk->count - (offset + 2);
    compiler->chunk->code[offset] = (uint8_t)((jump >> 8) & 0xFF);
    compiler->chunk->code[offset + 1] = (uint8_t)(jump & 0xFF);
}

static void emit_constant(Compiler* compiler, Value value) {
    int idx = add_constant(compiler->chunk, value);
    emit_byte(compiler, OP_CONST);
    emit_u16(compiler, (uint16_t)idx);
}

static int resolve_local(Compiler* compiler, const char* name, int length) {
    for (int i = compiler->local_count - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];
        if (local->length == length && memcmp(local->name, name, (size_t)length) == 0) {
            return i;
        }
    }
    return -1;
}

static int add_local(Compiler* compiler, const char* name, int length) {
    if (compiler->local_count >= MAX_LOCALS) return -1;
    Local* local = &compiler->locals[compiler->local_count];
    local->name = name;
    local->length = length;
    local->depth = compiler->scope_depth;
    return compiler->local_count++;
}

static void compile_expr(Compiler* compiler, Expr* expr);
static void compile_stmt(Compiler* compiler, Stmt* stmt);

static void compile_block(Compiler* compiler, Block* block) {
    for (int i = 0; i < block->stmt_count; i++) {
        compile_stmt(compiler, block->stmts[i]);
    }
}

static void compile_expr(Compiler* compiler, Expr* expr) {
    switch (expr->kind) {
        case EXPR_LITERAL:
            emit_constant(compiler, expr->as.literal.value);
            break;
        case EXPR_VARIABLE: {
            const char* name = expr->as.variable.name;
            int length = (int)strlen(name);
            int slot = resolve_local(compiler, name, length);
            if (slot < 0) {
                /* Unknown variable: silently produce no value for now. */
                return;
            }
            emit_byte(compiler, OP_GET_LOCAL);
            emit_byte(compiler, (uint8_t)slot);
            break;
        }
        case EXPR_BINARY: {
            BinaryExpr* b = &expr->as.binary;
            compile_expr(compiler, b->left);
            compile_expr(compiler, b->right);
            switch (b->op) {
                case TOKEN_PLUS:  emit_byte(compiler, OP_ADD); break;
                case TOKEN_MINUS: emit_byte(compiler, OP_SUB); break;
                case TOKEN_STAR:  emit_byte(compiler, OP_MUL); break;
                case TOKEN_SLASH: emit_byte(compiler, OP_DIV); break;
                case TOKEN_EQ:    emit_byte(compiler, OP_EQ);  break;
                case TOKEN_LT:    emit_byte(compiler, OP_LT);  break;
                case TOKEN_GT:    emit_byte(compiler, OP_GT);  break;
                default: break;
            }
            break;
        }
        default:
            /* Unsupported expression kind. */
            break;
    }
}

static void compile_stmt(Compiler* compiler, Stmt* stmt) {
    switch (stmt->kind) {
        case STMT_VAR_DECL: {
            VarDeclStmt* d = &stmt->as.var_decl;
            compile_expr(compiler, d->initializer);
            int slot = add_local(compiler, d->name, (int)strlen(d->name));
            if (slot < 0) return;
            emit_byte(compiler, OP_SET_LOCAL);
            emit_byte(compiler, (uint8_t)slot);
            break;
        }
        case STMT_ASSIGN: {
            AssignStmt* a = &stmt->as.assign;
            compile_expr(compiler, a->value);
            int slot = resolve_local(compiler, a->name, (int)strlen(a->name));
            if (slot < 0) return;
            emit_byte(compiler, OP_SET_LOCAL);
            emit_byte(compiler, (uint8_t)slot);
            break;
        }
        case STMT_RETURN:
            compile_expr(compiler, stmt->as.return_stmt.value);
            emit_byte(compiler, OP_RETURN);
            break;
        case STMT_IF: {
            IfStmt* i = &stmt->as.if_stmt;
            compile_expr(compiler, i->condition);
            int else_jump = emit_jump(compiler, OP_JZ);
            compile_block(compiler, i->then_block);
            int end_jump = emit_jump(compiler, OP_JMP);
            patch_jump(compiler, else_jump);
            if (i->else_block != NULL) {
                compile_block(compiler, i->else_block);
            }
            patch_jump(compiler, end_jump);
            break;
        }
        default:
            /* Unsupported statement kind. */
            break;
    }
}

int compile(const char* source, Chunk* chunk) {
    Program* program = parse(source);
    if (program == NULL) return 0;

    Compiler compiler;
    compiler.chunk = chunk;
    compiler.local_count = 0;
    compiler.scope_depth = 0;

    if (program->proc_count == 0) {
        emit_byte(&compiler, OP_RETURN);
    } else {
        compile_block(&compiler, program->procs[0].body);
        emit_byte(&compiler, OP_RETURN);
    }

    free_program(program);
    return 1;
}
