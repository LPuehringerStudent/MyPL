#include <stdlib.h>
#include <string.h>

#include "compiler.h"
#include "parser.h"

#define MAX_LOCALS 256
#define MAX_PROCS 256
#define MAX_CALL_PATCHES 1024

typedef struct {
    const char* name;
    int length;
    int depth;
} Local;

typedef struct {
    const char* name;
    int offset;
} ProcEntry;

typedef struct {
    int offset;
    const char* name;
} CallPatch;

typedef struct {
    Chunk* chunk;
    Local locals[MAX_LOCALS];
    int local_count;
    int scope_depth;
    ProcEntry procs[MAX_PROCS];
    int proc_count;
    CallPatch patches[MAX_CALL_PATCHES];
    int patch_count;
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

static void patch_jump_to(Compiler* compiler, int offset, int target) {
    int jump = target - (offset + 2);
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

static int find_proc(Compiler* compiler, const char* name) {
    for (int i = 0; i < compiler->proc_count; i++) {
        if (strcmp(compiler->procs[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static void patch_call(Chunk* chunk, int offset, int target) {
    chunk->code[offset] = (uint8_t)((target >> 8) & 0xFF);
    chunk->code[offset + 1] = (uint8_t)(target & 0xFF);
}

static void emit_call(Compiler* compiler, const char* name, int arg_count) {
    int idx = find_proc(compiler, name);
    emit_byte(compiler, OP_CALL);
    if (idx >= 0) {
        emit_u16(compiler, (uint16_t)compiler->procs[idx].offset);
    } else {
        int patch = compiler->patch_count++;
        compiler->patches[patch].offset = compiler->chunk->count;
        compiler->patches[patch].name = name;
        emit_u16(compiler, 0);
    }
    emit_byte(compiler, (uint8_t)arg_count);
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
        case EXPR_LITERAL: {
            Value v = expr->as.literal.value;
            if (v.type == VAL_STRING && v.as.as_string != NULL) {
                char* copy = malloc(strlen(v.as.as_string) + 1);
                if (copy == NULL) return;
                strcpy(copy, v.as.as_string);
                v.as.as_string = copy;
            }
            emit_constant(compiler, v);
            break;
        }
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
        case EXPR_CALL: {
            CallExpr* c = &expr->as.call;
            for (int i = 0; i < c->arg_count; i++) {
                compile_expr(compiler, c->args[i]);
            }
            emit_call(compiler, c->name, c->arg_count);
            break;
        }
        case EXPR_FIELD: {
            FieldExpr* f = &expr->as.field;
            char* field_name = malloc((size_t)strlen(f->field) + 1);
            if (field_name == NULL) return;
            strcpy(field_name, f->field);
            int field_idx = add_constant(compiler->chunk, value_string(field_name));
            if (field_idx < 0) {
                free(field_name);
                return;
            }
            emit_byte(compiler, OP_GET_FIELD);
            emit_u16(compiler, (uint16_t)field_idx);
            break;
        }
        case EXPR_UNARY: {
            UnaryExpr* u = &expr->as.unary;
            compile_expr(compiler, u->operand);
            switch (u->op) {
                case TOKEN_MINUS: emit_byte(compiler, OP_NEGATE); break;
                case TOKEN_BANG:  emit_byte(compiler, OP_NOT); break;
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
        case STMT_FOR: {
            ForStmt* f = &stmt->as.for_stmt;
            char* query = malloc((size_t)strlen(f->sql_query) + 1);
            if (query == NULL) return;
            strcpy(query, f->sql_query);
            int query_idx = add_constant(compiler->chunk, value_string(query));
            if (query_idx < 0) {
                free(query);
                return;
            }
            emit_byte(compiler, OP_SQL);
            emit_u16(compiler, (uint16_t)query_idx);

            int loop_start = compiler->chunk->count;
            int exit_jump = emit_jump(compiler, OP_SQL_NEXT);

            /* Bind iterator variable to a local slot. The value is unused
               because fields are read with OP_GET_FIELD, but we need a slot
               so any nested locals keep correct offsets. */
            int slot = add_local(compiler, f->var_name, (int)strlen(f->var_name));
            if (slot < 0) return;
            emit_byte(compiler, OP_CONST);
            emit_u16(compiler, (uint16_t)add_constant(compiler->chunk, value_int(0)));
            emit_byte(compiler, OP_SET_LOCAL);
            emit_byte(compiler, (uint8_t)slot);

            compile_block(compiler, f->body);

            int back = emit_jump(compiler, OP_JMP);
            patch_jump_to(compiler, back, loop_start);
            patch_jump(compiler, exit_jump);
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
    compiler.proc_count = 0;
    compiler.patch_count = 0;

    if (program->proc_count == 0) {
        emit_byte(&compiler, OP_RETURN);
        free_program(program);
        return 1;
    }

    int entry_index = 0;
    for (int i = 0; i < program->proc_count; i++) {
        if (strcmp(program->procs[i].name, "main") == 0) {
            entry_index = i;
            break;
        }
    }

    /* Top-level: call entry procedure, then halt. */
    int entry_patch = chunk->count + 1;
    emit_byte(&compiler, OP_CALL);
    emit_u16(&compiler, 0);
    emit_byte(&compiler, 0); /* arg count */
    emit_byte(&compiler, OP_RETURN);

    /* Emit procedure bodies and record their offsets. */
    for (int i = 0; i < program->proc_count; i++) {
        compiler.procs[compiler.proc_count].name = program->procs[i].name;
        compiler.procs[compiler.proc_count].offset = chunk->count;
        compiler.proc_count++;
        for (int p = 0; p < program->procs[i].param_count; p++) {
            Param* param = &program->procs[i].params[p];
            add_local(&compiler, param->name, (int)strlen(param->name));
        }
        compile_block(&compiler, program->procs[i].body);
        emit_byte(&compiler, OP_RETURN);
        compiler.local_count = 0;
    }

    /* Patch top-level entry call. */
    patch_call(chunk, entry_patch, compiler.procs[entry_index].offset);

    /* Patch forward/out-of-order procedure calls. */
    for (int i = 0; i < compiler.patch_count; i++) {
        int idx = find_proc(&compiler, compiler.patches[i].name);
        if (idx < 0) {
            free_program(program);
            return 0;
        }
        patch_call(chunk, compiler.patches[i].offset, compiler.procs[idx].offset);
    }

    free_program(program);
    return 1;
}
