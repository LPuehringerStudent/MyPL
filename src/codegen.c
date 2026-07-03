#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "compiler.h"
#include "natives.h"
#include "os.h"
#include "parser.h"
#include "typecheck.h"

#define MAX_LOCALS 256
#define MAX_PROCS 256
#define MAX_CALL_PATCHES 1024
#define MAX_MODULES 64
#define MAX_LOADING 64
#define MAX_LOOP_NESTING 64
#define MAX_BREAK_PATCHES 256

typedef struct {
    const char* name;
    int length;
    int depth;
    Type* type;
} Local;

typedef struct {
    int continue_target;
    int local_count_at_start;
    int continue_local_count;
    int break_patches[MAX_BREAK_PATCHES];
    int break_count;
} LoopContext;

typedef struct {
    const char* name;
    int offset;
    Type* return_type;
    Type** param_types;
    int param_count;
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
    int had_error;
    char error_message[256];
    int entry_index;
    int entry_patch;
    char* loaded_paths[MAX_MODULES];
    int loaded_count;
    const char* loading_stack[MAX_LOADING];
    int loading_count;
    char* current_path;
    struct Context* ctx;
    LoopContext loops[MAX_LOOP_NESTING];
    int loop_count;
    int current_line;
} Compiler;

static int add_proc_entry(Compiler* compiler, const char* name, int offset,
                          Type* return_type, Type** param_types, int param_count) {
    if (compiler->proc_count >= MAX_PROCS) return -3;
    for (int i = 0; i < compiler->proc_count; i++) {
        if (strcmp(compiler->procs[i].name, name) == 0) return -2;
    }
    char* name_copy = malloc(strlen(name) + 1);
    if (name_copy == NULL) return -1;
    strcpy(name_copy, name);
    Type* rt_copy = type_copy(return_type);
    if (rt_copy == NULL && return_type != NULL) { free(name_copy); return -1; }
    Type** pt_copy = NULL;
    if (param_count > 0) {
        pt_copy = malloc(sizeof(Type*) * (size_t)param_count);
        if (pt_copy == NULL) { free(name_copy); type_free(rt_copy); return -1; }
        for (int i = 0; i < param_count; i++) {
            pt_copy[i] = type_copy(param_types[i]);
            if (pt_copy[i] == NULL && param_types[i] != NULL) {
                for (int j = 0; j < i; j++) type_free(pt_copy[j]);
                free(pt_copy);
                free(name_copy);
                type_free(rt_copy);
                return -1;
            }
        }
    }
    compiler->procs[compiler->proc_count].name = name_copy;
    compiler->procs[compiler->proc_count].offset = offset;
    compiler->procs[compiler->proc_count].return_type = rt_copy;
    compiler->procs[compiler->proc_count].param_types = pt_copy;
    compiler->procs[compiler->proc_count].param_count = param_count;
    return compiler->proc_count++;
}

static void free_proc_entries(Compiler* compiler) {
    for (int i = 0; i < compiler->proc_count; i++) {
        free((void*)compiler->procs[i].name);
        type_free(compiler->procs[i].return_type);
        for (int p = 0; p < compiler->procs[i].param_count; p++) {
            type_free(compiler->procs[i].param_types[p]);
        }
        free(compiler->procs[i].param_types);
    }
}

static int add_call_patch(Compiler* compiler, const char* name, int offset) {
    if (compiler->patch_count >= MAX_CALL_PATCHES) return 0;
    char* copy = malloc(strlen(name) + 1);
    if (copy == NULL) return 0;
    strcpy(copy, name);
    compiler->patches[compiler->patch_count].offset = offset;
    compiler->patches[compiler->patch_count].name = copy;
    compiler->patch_count++;
    return 1;
}

static void error(Compiler* compiler, const char* message) {
    if (compiler->had_error) return;
    compiler->had_error = 1;
    strncpy(compiler->error_message, message, sizeof(compiler->error_message) - 1);
    compiler->error_message[sizeof(compiler->error_message) - 1] = '\0';
}

static void emit_byte(Compiler* compiler, uint8_t byte) {
    write_chunk_line(compiler->chunk, byte, compiler->current_line);
}

static void emit_u16(Compiler* compiler, uint16_t value) {
    write_chunk_u16_line(compiler->chunk, value, compiler->current_line);
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

static int push_loop(Compiler* compiler, int continue_target,
                     int local_count_at_start, int continue_local_count) {
    if (compiler->loop_count >= MAX_LOOP_NESTING) {
        error(compiler, "too many nested loops");
        return 0;
    }
    LoopContext* loop = &compiler->loops[compiler->loop_count++];
    loop->continue_target = continue_target;
    loop->local_count_at_start = local_count_at_start;
    loop->continue_local_count = continue_local_count;
    loop->break_count = 0;
    return 1;
}

static void pop_loop(Compiler* compiler) {
    LoopContext* loop = &compiler->loops[compiler->loop_count - 1];
    int target = compiler->chunk->count;
    for (int i = 0; i < loop->break_count; i++) {
        patch_jump_to(compiler, loop->break_patches[i], target);
    }
    compiler->loop_count--;
}

static void emit_break(Compiler* compiler) {
    if (compiler->loop_count == 0) {
        error(compiler, "break outside loop");
        return;
    }
    LoopContext* loop = &compiler->loops[compiler->loop_count - 1];
    if (loop->break_count >= MAX_BREAK_PATCHES) {
        error(compiler, "too many break statements in loop");
        return;
    }
    int pops = compiler->local_count - loop->local_count_at_start;
    for (int i = 0; i < pops; i++) {
        emit_byte(compiler, OP_POP);
    }
    loop->break_patches[loop->break_count++] = emit_jump(compiler, OP_JMP);
}

static void emit_continue(Compiler* compiler) {
    if (compiler->loop_count == 0) {
        error(compiler, "continue outside loop");
        return;
    }
    LoopContext* loop = &compiler->loops[compiler->loop_count - 1];
    int pops = compiler->local_count - loop->continue_local_count;
    for (int i = 0; i < pops; i++) {
        emit_byte(compiler, OP_POP);
    }
    int offset = emit_jump(compiler, OP_JMP);
    patch_jump_to(compiler, offset, loop->continue_target);
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

static Type* resolve_local_type(Compiler* compiler, const char* name, int length) {
    for (int i = compiler->local_count - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];
        if (local->length == length && memcmp(local->name, name, (size_t)length) == 0) {
            return local->type;
        }
    }
    return NULL;
}

static int add_local(Compiler* compiler, const char* name, int length, Type* type) {
    if (compiler->local_count >= MAX_LOCALS) return -1;
    Local* local = &compiler->locals[compiler->local_count];
    local->name = name;
    local->length = length;
    local->depth = compiler->scope_depth;
    local->type = type;
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
    emit_byte(compiler, OP_CALL);
    int idx = find_proc(compiler, name);
    if (idx >= 0 && compiler->procs[idx].offset >= 0) {
        emit_u16(compiler, (uint16_t)compiler->procs[idx].offset);
    } else {
        if (!add_call_patch(compiler, name, compiler->chunk->count)) {
            error(compiler, "too many call patches");
            return;
        }
        emit_u16(compiler, 0);
    }
    emit_byte(compiler, (uint8_t)arg_count);
}

static void compile_expr(Compiler* compiler, Expr* expr);
static void compile_stmt(Compiler* compiler, Stmt* stmt);

static int compiler_load_module(Compiler* compiler, const char* path, char* error, size_t error_size);
static int compiler_compile_source(Compiler* compiler, const char* source, int is_main, const char* path, char* error, size_t error_size);

static void compile_block(Compiler* compiler, Block* block) {
    int previous_count = compiler->local_count;
    for (int i = 0; i < block->stmt_count; i++) {
        compile_stmt(compiler, block->stmts[i]);
        if (compiler->had_error) return;
    }
    int pops = compiler->local_count - previous_count;
    for (int i = 0; i < pops; i++) {
        emit_byte(compiler, OP_POP);
    }
    compiler->local_count = previous_count;
}

static void compile_expr(Compiler* compiler, Expr* expr) {
    if (expr != NULL && expr->loc.line > 0) {
        compiler->current_line = expr->loc.line;
    }
    switch (expr->kind) {
        case EXPR_LITERAL: {
            Value v = expr->as.literal.value;
            emit_constant(compiler, v);
            break;
        }
        case EXPR_ARRAY: {
            ArrayExpr* a = &expr->as.array;
            for (int i = 0; i < a->count; i++) {
                compile_expr(compiler, a->elements[i]);
                if (compiler->had_error) return;
            }
            emit_byte(compiler, OP_ARRAY_BUILD);
            emit_u16(compiler, (uint16_t)a->count);
            break;
        }
        case EXPR_INDEX: {
            IndexExpr* idx = &expr->as.index;
            compile_expr(compiler, idx->array);
            if (compiler->had_error) return;
            compile_expr(compiler, idx->index);
            if (compiler->had_error) return;
            emit_byte(compiler, OP_INDEX_GET);
            break;
        }
        case EXPR_VARIABLE: {
            const char* name = expr->as.variable.name;
            int length = (int)strlen(name);
            int slot = resolve_local(compiler, name, length);
            if (slot < 0) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Undefined variable '%s'", name);
                error(compiler, msg);
                return;
            }
            emit_byte(compiler, OP_GET_LOCAL);
            emit_byte(compiler, (uint8_t)slot);
            break;
        }
        case EXPR_BINARY: {
            BinaryExpr* b = &expr->as.binary;
            compile_expr(compiler, b->left);
            if (compiler->had_error) return;
            compile_expr(compiler, b->right);
            if (compiler->had_error) return;
            switch (b->op) {
                case TOKEN_PLUS:  emit_byte(compiler, OP_ADD); break;
                case TOKEN_MINUS: emit_byte(compiler, OP_SUB); break;
                case TOKEN_STAR:  emit_byte(compiler, OP_MUL); break;
                case TOKEN_SLASH: emit_byte(compiler, OP_DIV); break;
                case TOKEN_EQ:    emit_byte(compiler, OP_EQ);  break;
                case TOKEN_LT:    emit_byte(compiler, OP_LT);  break;
                case TOKEN_GT:    emit_byte(compiler, OP_GT);  break;
                default: error(compiler, "unsupported binary operator"); break;
            }
            break;
        }
        case EXPR_CALL: {
            CallExpr* c = &expr->as.call;
            int native_idx = native_find(c->name);
            if (native_idx >= 0) {
                int expected_arity = native_arity(native_idx);
                if (c->arg_count != expected_arity) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "%s expects %d argument(s)", c->name, expected_arity);
                    error(compiler, msg);
                    return;
                }
                for (int i = 0; i < c->arg_count; i++) {
                    compile_expr(compiler, c->args[i]);
                    if (compiler->had_error) return;
                }
                emit_byte(compiler, OP_NATIVE_CALL);
                emit_u16(compiler, (uint16_t)native_idx);
                emit_byte(compiler, (uint8_t)c->arg_count);
            } else {
                for (int i = 0; i < c->arg_count; i++) {
                    compile_expr(compiler, c->args[i]);
                    if (compiler->had_error) return;
                }
                emit_call(compiler, c->name, c->arg_count);
            }
            break;
        }
        case EXPR_FIELD: {
            FieldExpr* f = &expr->as.field;
            char* field_name = malloc((size_t)strlen(f->field) + 1);
            if (field_name == NULL) {
                error(compiler, "out of memory");
                return;
            }
            strcpy(field_name, f->field);
            int field_idx = add_constant(compiler->chunk, value_string(field_name));
            if (field_idx < 0) {
                free(field_name);
                error(compiler, "too many constants");
                return;
            }
            emit_byte(compiler, OP_GET_FIELD);
            emit_u16(compiler, (uint16_t)field_idx);
            break;
        }
        case EXPR_UNARY: {
            UnaryExpr* u = &expr->as.unary;
            compile_expr(compiler, u->operand);
            if (compiler->had_error) return;
            switch (u->op) {
                case TOKEN_MINUS: emit_byte(compiler, OP_NEGATE); break;
                case TOKEN_BANG:  emit_byte(compiler, OP_NOT); break;
                default: error(compiler, "unsupported unary operator"); break;
            }
            break;
        }
        case EXPR_SQL_PARAM: {
            const char* name = expr->as.sql_param.name;
            int slot = resolve_local(compiler, name, (int)strlen(name));
            if (slot < 0) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Undefined variable '%s'", name);
                error(compiler, msg);
                return;
            }
            emit_byte(compiler, OP_GET_LOCAL);
            emit_byte(compiler, (uint8_t)slot);
            break;
        }
        case EXPR_ROW_FIELD: {
            RowFieldExpr* rf = &expr->as.row_field;
            compile_expr(compiler, rf->row);
            if (compiler->had_error) return;
            char* field_name = malloc((size_t)strlen(rf->field) + 1);
            if (field_name == NULL) {
                error(compiler, "out of memory");
                return;
            }
            strcpy(field_name, rf->field);
            int field_idx = add_constant(compiler->chunk, value_string(field_name));
            if (field_idx < 0) {
                free(field_name);
                error(compiler, "too many constants");
                return;
            }
            emit_byte(compiler, OP_ROW_GET);
            emit_u16(compiler, (uint16_t)field_idx);
            break;
        }
        default:
            error(compiler, "unsupported expression");
            break;
    }
}

static void compile_stmt(Compiler* compiler, Stmt* stmt) {
    if (stmt != NULL && stmt->loc.line > 0) {
        compiler->current_line = stmt->loc.line;
    }
    switch (stmt->kind) {
        case STMT_VAR_DECL: {
            VarDeclStmt* d = &stmt->as.var_decl;
            compile_expr(compiler, d->initializer);
            if (compiler->had_error) return;
            int slot = add_local(compiler, d->name, (int)strlen(d->name), d->type);
            if (slot < 0) {
                error(compiler, "too many locals");
                return;
            }
            emit_byte(compiler, OP_SET_LOCAL);
            emit_byte(compiler, (uint8_t)slot);
            break;
        }
        case STMT_ASSIGN: {
            AssignStmt* a = &stmt->as.assign;
            compile_expr(compiler, a->value);
            if (compiler->had_error) return;
            int slot = resolve_local(compiler, a->name, (int)strlen(a->name));
            if (slot < 0) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Undefined variable '%s'", a->name);
                error(compiler, msg);
                return;
            }
            emit_byte(compiler, OP_SET_LOCAL);
            emit_byte(compiler, (uint8_t)slot);
            break;
        }
        case STMT_RETURN:
            compile_expr(compiler, stmt->as.return_stmt.value);
            if (compiler->had_error) return;
            emit_byte(compiler, OP_RETURN);
            break;
        case STMT_IF: {
            IfStmt* i = &stmt->as.if_stmt;
            compile_expr(compiler, i->condition);
            if (compiler->had_error) return;
            int else_jump = emit_jump(compiler, OP_JZ);
            compile_block(compiler, i->then_block);
            if (compiler->had_error) return;
            int end_jump = emit_jump(compiler, OP_JMP);
            patch_jump(compiler, else_jump);
            if (i->else_block != NULL) {
                compile_block(compiler, i->else_block);
                if (compiler->had_error) return;
            }
            patch_jump(compiler, end_jump);
            break;
        }
        case STMT_WHILE: {
            WhileStmt* w = &stmt->as.while_stmt;
            int loop_start = compiler->chunk->count;
            int start_count = compiler->local_count;
            if (!push_loop(compiler, loop_start, start_count, start_count)) return;
            compile_expr(compiler, w->condition);
            if (compiler->had_error) return;
            int exit_jump = emit_jump(compiler, OP_JZ);
            compile_block(compiler, w->body);
            if (compiler->had_error) return;
            int back = emit_jump(compiler, OP_JMP);
            patch_jump_to(compiler, back, loop_start);
            patch_jump(compiler, exit_jump);
            pop_loop(compiler);
            break;
        }
        case STMT_FOREACH: {
            ForeachStmt* f = &stmt->as.foreach_stmt;
            int start_count = compiler->local_count;
            compile_expr(compiler, f->iterable);
            if (compiler->had_error) return;
            int array_slot = add_local(compiler, "__fe_array", 12, &type_unknown);
            if (array_slot < 0) { error(compiler, "too many locals"); return; }
            emit_byte(compiler, OP_SET_LOCAL);
            emit_byte(compiler, (uint8_t)array_slot);
            emit_constant(compiler, value_int(0));
            int idx_slot = add_local(compiler, "__fe_idx", 9, &type_int);
            emit_byte(compiler, OP_SET_LOCAL);
            emit_byte(compiler, (uint8_t)idx_slot);
            emit_constant(compiler, value_int(0));
            int var_slot = add_local(compiler, f->var_name, (int)strlen(f->var_name), &type_unknown);
            emit_byte(compiler, OP_SET_LOCAL);
            emit_byte(compiler, (uint8_t)var_slot);
            int loop_start = compiler->chunk->count;
            if (!push_loop(compiler, loop_start, start_count, start_count + 3)) return;
            emit_byte(compiler, OP_GET_LOCAL);
            emit_byte(compiler, (uint8_t)idx_slot);
            emit_byte(compiler, OP_GET_LOCAL);
            emit_byte(compiler, (uint8_t)array_slot);
            int len_native = native_find("length");
            emit_byte(compiler, OP_NATIVE_CALL);
            emit_u16(compiler, (uint16_t)len_native);
            emit_byte(compiler, 1);
            emit_byte(compiler, OP_LT);
            int exit_jump = emit_jump(compiler, OP_JZ);
            emit_byte(compiler, OP_GET_LOCAL);
            emit_byte(compiler, (uint8_t)array_slot);
            emit_byte(compiler, OP_GET_LOCAL);
            emit_byte(compiler, (uint8_t)idx_slot);
            emit_byte(compiler, OP_INDEX_GET);
            emit_byte(compiler, OP_SET_LOCAL);
            emit_byte(compiler, (uint8_t)var_slot);
            emit_byte(compiler, OP_POP);
            compile_block(compiler, f->body);
            if (compiler->had_error) return;
            emit_byte(compiler, OP_GET_LOCAL);
            emit_byte(compiler, (uint8_t)idx_slot);
            emit_constant(compiler, value_int(1));
            emit_byte(compiler, OP_ADD);
            emit_byte(compiler, OP_SET_LOCAL);
            emit_byte(compiler, (uint8_t)idx_slot);
            emit_byte(compiler, OP_POP);
            int back = emit_jump(compiler, OP_JMP);
            patch_jump_to(compiler, back, loop_start);
            patch_jump(compiler, exit_jump);
            pop_loop(compiler);
            for (int i = 0; i < 3; i++) emit_byte(compiler, OP_POP);
            compiler->local_count = start_count;
            break;
        }
        case STMT_BREAK:
            emit_break(compiler);
            break;
        case STMT_CONTINUE:
            emit_continue(compiler);
            break;
        case STMT_FOR: {
            ForStmt* f = &stmt->as.for_stmt;

            for (int i = 0; i < f->param_count; i++) {
                compile_expr(compiler, f->params[i]);
                if (compiler->had_error) return;
                const char* name = f->params[i]->as.sql_param.name;
                Type* t = resolve_local_type(compiler, name, (int)strlen(name));
                if (t != NULL && t->kind == TYPE_STRING) {
                    emit_byte(compiler, OP_SQL_BIND_STRING);
                } else if (t != NULL && t->kind == TYPE_FLOAT) {
                    emit_byte(compiler, OP_SQL_BIND_FLOAT);
                } else {
                    emit_byte(compiler, OP_SQL_BIND_INT);
                }
            }

            char* query = malloc((size_t)strlen(f->sql_query) + 1);
            if (query == NULL) {
                error(compiler, "out of memory");
                return;
            }
            strcpy(query, f->sql_query);
            int query_idx = add_constant(compiler->chunk, value_string(query));
            if (query_idx < 0) {
                free(query);
                error(compiler, "too many constants");
                return;
            }
            emit_byte(compiler, OP_SQL);
            emit_u16(compiler, (uint16_t)query_idx);
            emit_u16(compiler, (uint16_t)stmt->loc.line);

            int loop_start = compiler->chunk->count;
            int exit_jump = emit_jump(compiler, OP_SQL_NEXT);

            /* Bind iterator variable to a local slot. The value is unused
               because fields are read with OP_GET_FIELD, but we need a slot
               so any nested locals keep correct offsets. */
            int slot = add_local(compiler, f->var_name, (int)strlen(f->var_name), &type_unknown);
            if (slot < 0) {
                error(compiler, "too many locals");
                return;
            }
            emit_byte(compiler, OP_CONST);
            emit_u16(compiler, (uint16_t)add_constant(compiler->chunk, value_int(0)));
            emit_byte(compiler, OP_SET_LOCAL);
            emit_byte(compiler, (uint8_t)slot);

            compile_block(compiler, f->body);
            if (compiler->had_error) return;

            int back = emit_jump(compiler, OP_JMP);
            patch_jump_to(compiler, back, loop_start);
            patch_jump(compiler, exit_jump);

            /* Pop the iterator local at loop exit so it does not leak. */
            emit_byte(compiler, OP_POP);
            compiler->local_count--;
            break;
        }
        case STMT_PRINT: {
            compile_expr(compiler, stmt->as.print_stmt.value);
            if (compiler->had_error) return;
            emit_byte(compiler, OP_PRINT);
            break;
        }
        case STMT_INDEX_ASSIGN: {
            IndexAssignStmt* ia = &stmt->as.index_assign;
            compile_expr(compiler, ia->array);
            if (compiler->had_error) return;
            compile_expr(compiler, ia->index);
            if (compiler->had_error) return;
            compile_expr(compiler, ia->value);
            if (compiler->had_error) return;
            emit_byte(compiler, OP_INDEX_SET);
            break;
        }
        case STMT_EXPR: {
            compile_expr(compiler, stmt->as.expr_stmt.value);
            if (compiler->had_error) return;
            emit_byte(compiler, OP_POP);
            break;
        }
        case STMT_SQL_DDL:
        case STMT_SQL_DML: {
            SqlStmt* s = &stmt->as.sql_stmt;
            for (int i = 0; i < s->param_count; i++) {
                compile_expr(compiler, s->params[i]);
                if (compiler->had_error) return;
                const char* name = s->params[i]->as.sql_param.name;
                Type* t = resolve_local_type(compiler, name, (int)strlen(name));
                if (t != NULL && t->kind == TYPE_STRING) {
                    emit_byte(compiler, OP_SQL_BIND_STRING);
                } else if (t != NULL && t->kind == TYPE_FLOAT) {
                    emit_byte(compiler, OP_SQL_BIND_FLOAT);
                } else {
                    emit_byte(compiler, OP_SQL_BIND_INT);
                }
            }
            char* sql_copy = malloc((size_t)strlen(s->sql) + 1);
            if (sql_copy == NULL) {
                error(compiler, "out of memory");
                return;
            }
            strcpy(sql_copy, s->sql);
            int sql_idx = add_constant(compiler->chunk, value_string(sql_copy));
            if (sql_idx < 0) {
                free(sql_copy);
                error(compiler, "too many constants");
                return;
            }
            emit_byte(compiler, OP_SQL_EXEC);
            emit_u16(compiler, (uint16_t)sql_idx);
            emit_u16(compiler, (uint16_t)stmt->loc.line);
            break;
        }
        case STMT_SQL_TRANSACTION: {
            int kind = stmt->as.sql_transaction.kind;
            if (kind == 0) {
                emit_byte(compiler, OP_SQL_BEGIN);
            } else if (kind == 1) {
                emit_byte(compiler, OP_SQL_COMMIT);
            } else {
                emit_byte(compiler, OP_SQL_ROLLBACK);
            }
            break;
        }
        case STMT_SQL_QUERY: {
            SqlStmt* s = &stmt->as.sql_stmt;
            if (s->into_count == 0) {
                error(compiler, "SELECT statement requires INTO");
                return;
            }
            for (int i = 0; i < s->into_count; i++) {
                int slot = resolve_local(compiler, s->into_vars[i], (int)strlen(s->into_vars[i]));
                if (slot < 0) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "Undefined variable '%s'", s->into_vars[i]);
                    error(compiler, msg);
                    return;
                }
            }
            int into_array = 0;
            if (s->into_count == 1) {
                Type* t = resolve_local_type(compiler, s->into_vars[0], (int)strlen(s->into_vars[0]));
                if (t != NULL && t->kind == TYPE_ARRAY && t->element_type != NULL && t->element_type->kind == TYPE_ROW) {
                    into_array = 1;
                }
            }
            for (int i = 0; i < s->param_count; i++) {
                compile_expr(compiler, s->params[i]);
                if (compiler->had_error) return;
                const char* name = s->params[i]->as.sql_param.name;
                Type* t = resolve_local_type(compiler, name, (int)strlen(name));
                if (t != NULL && t->kind == TYPE_STRING) {
                    emit_byte(compiler, OP_SQL_BIND_STRING);
                } else if (t != NULL && t->kind == TYPE_FLOAT) {
                    emit_byte(compiler, OP_SQL_BIND_FLOAT);
                } else {
                    emit_byte(compiler, OP_SQL_BIND_INT);
                }
            }
            char* sql_copy = malloc((size_t)strlen(s->sql) + 1);
            if (sql_copy == NULL) {
                error(compiler, "out of memory");
                return;
            }
            strcpy(sql_copy, s->sql);
            int sql_idx = add_constant(compiler->chunk, value_string(sql_copy));
            if (sql_idx < 0) {
                free(sql_copy);
                error(compiler, "too many constants");
                return;
            }
            emit_byte(compiler, OP_SQL);
            emit_u16(compiler, (uint16_t)sql_idx);
            emit_u16(compiler, (uint16_t)stmt->loc.line);

            if (into_array) {
                int slot = resolve_local(compiler, s->into_vars[0], (int)strlen(s->into_vars[0]));
                emit_byte(compiler, OP_SQL_TO_ARRAY);
                emit_byte(compiler, OP_SET_LOCAL);
                emit_byte(compiler, (uint8_t)slot);
                emit_byte(compiler, OP_POP);
            } else {
                int exit_jump = emit_jump(compiler, OP_SQL_NEXT);

                for (int i = 0; i < s->into_count; i++) {
                    int slot = resolve_local(compiler, s->into_vars[i], (int)strlen(s->into_vars[i]));
                    emit_byte(compiler, OP_SQL_GET_COLUMN);
                    emit_u16(compiler, (uint16_t)i);
                    emit_byte(compiler, OP_SET_LOCAL);
                    emit_byte(compiler, (uint8_t)slot);
                    emit_byte(compiler, OP_POP);
                }

                int skip_error = emit_jump(compiler, OP_JMP);
                patch_jump(compiler, exit_jump);

                const char* msg = "SELECT INTO found no matching row";
                char* msg_copy = malloc(strlen(msg) + 1);
                if (msg_copy == NULL) {
                    error(compiler, "out of memory");
                    return;
                }
                strcpy(msg_copy, msg);
                int msg_idx = add_constant(compiler->chunk, value_string(msg_copy));
                if (msg_idx < 0) {
                    free(msg_copy);
                    error(compiler, "too many constants");
                    return;
                }
                emit_byte(compiler, OP_RUNTIME_ERROR);
                emit_u16(compiler, (uint16_t)msg_idx);

                patch_jump(compiler, skip_error);
            }
            break;
        }
        default:
            error(compiler, "unsupported statement");
            break;
    }
}

static int is_module_loaded(Compiler* compiler, const char* path) {
    for (int i = 0; i < compiler->loaded_count; i++) {
        if (strcmp(compiler->loaded_paths[i], path) == 0) return 1;
    }
    return 0;
}

static int is_module_loading(Compiler* compiler, const char* path) {
    for (int i = 0; i < compiler->loading_count; i++) {
        if (strcmp(compiler->loading_stack[i], path) == 0) return 1;
    }
    return 0;
}

static char* path_directory(const char* path) {
    char* copy = strdup(path);
    if (copy == NULL) return NULL;
    char* dir = dirname(copy);
    char* result = strdup(dir);
    free(copy);
    return result;
}

static char* resolve_import_path(const char* base_dir, const char* import_path) {
    if (import_path[0] == '/') {
        return realpath(import_path, NULL);
    }

    char combined[4096];
    if (base_dir != NULL) {
        snprintf(combined, sizeof(combined), "%s/%s", base_dir, import_path);
    } else {
        if (getcwd(combined, sizeof(combined)) == NULL) return NULL;
        size_t len = strlen(combined);
        snprintf(combined + len, sizeof(combined) - len, "/%s", import_path);
    }
    return realpath(combined, NULL);
}

static int compiler_load_module(Compiler* compiler, const char* import_path, char* error, size_t error_size) {
    char* resolved = resolve_import_path(compiler->current_path, import_path);
    if (resolved == NULL) {
        if (error != NULL && error_size > 0) {
            snprintf(error, error_size, "Module not found: %s", import_path);
        }
        return 0;
    }

    if (is_module_loading(compiler, resolved)) {
        free(resolved);
        if (error != NULL && error_size > 0) {
            snprintf(error, error_size, "Circular import of '%s'", import_path);
        }
        return 0;
    }
    if (is_module_loaded(compiler, resolved)) {
        free(resolved);
        return 1;
    }
    if (compiler->loaded_count >= MAX_MODULES) {
        free(resolved);
        if (error != NULL && error_size > 0) {
            snprintf(error, error_size, "Too many imported modules");
        }
        return 0;
    }
    if (!os_file_exists(resolved)) {
        free(resolved);
        if (error != NULL && error_size > 0) {
            snprintf(error, error_size, "Module not found: %s", import_path);
        }
        return 0;
    }

    char* source = os_read_file(resolved);
    if (source == NULL) {
        free(resolved);
        if (error != NULL && error_size > 0) {
            snprintf(error, error_size, "Could not read module: %s", import_path);
        }
        return 0;
    }

    if (compiler->loading_count >= MAX_LOADING) {
        free(resolved);
        free(source);
        if (error != NULL && error_size > 0) {
            snprintf(error, error_size, "Too many nested imports");
        }
        return 0;
    }
    compiler->loading_stack[compiler->loading_count++] = resolved;

    int ok = compiler_compile_source(compiler, source, 0, resolved, error, error_size);

    compiler->loading_count--;
    if (ok) {
        compiler->loaded_paths[compiler->loaded_count++] = resolved;
    } else {
        free(resolved);
    }
    free(source);
    return ok;
}

static int do_compile_source(Compiler* compiler, const char* source, int is_main, char* error, size_t error_size) {
    char parse_error[256] = {0};
    Program* program = parse(source, parse_error, sizeof(parse_error));
    if (program == NULL) {
        if (error != NULL && error_size > 0) {
            strncpy(error, parse_error, error_size - 1);
            error[error_size - 1] = '\0';
        }
        return 0;
    }

    for (int i = 0; i < program->import_count; i++) {
        const char* path = program->imports[i]->as.import_stmt.path;
        if (!compiler_load_module(compiler, path, error, error_size)) {
            free_program(program);
            return 0;
        }
    }

    for (int i = 0; i < program->proc_count; i++) {
        ProcDecl* proc = &program->procs[i];
        Type** pts = NULL;
        if (proc->param_count > 0) {
            pts = malloc(sizeof(Type*) * (size_t)proc->param_count);
            for (int p = 0; p < proc->param_count; p++) pts[p] = proc->params[p].type;
        }
        int idx = add_proc_entry(compiler, proc->name, -1, proc->return_type, pts, proc->param_count);
        free(pts);
        if (idx < 0) {
            if (error != NULL && error_size > 0) {
                if (idx == -1) {
                    snprintf(error, error_size, "Out of memory registering procedure '%s'", proc->name);
                } else if (idx == -2) {
                    snprintf(error, error_size, "Duplicate procedure '%s'", proc->name);
                } else {
                    snprintf(error, error_size, "Too many procedures");
                }
            }
            free_program(program);
            return 0;
        }
        if (is_main && strcmp(proc->name, "main") == 0) {
            compiler->entry_index = idx;
        }
    }

    ProcSignature* sigs = malloc(sizeof(ProcSignature) * (size_t)compiler->proc_count);
    if (sigs == NULL) {
        if (error != NULL && error_size > 0) {
            strncpy(error, "out of memory", error_size - 1);
            error[error_size - 1] = '\0';
        }
        free_program(program);
        return 0;
    }
    for (int i = 0; i < compiler->proc_count; i++) {
        sigs[i].name = compiler->procs[i].name;
        sigs[i].return_type = compiler->procs[i].return_type;
        sigs[i].param_types = compiler->procs[i].param_types;
        sigs[i].param_count = compiler->procs[i].param_count;
    }

    if (!typecheck_program(program, sigs, compiler->proc_count, compiler->ctx, error, error_size)) {
        free(sigs);
        free_program(program);
        return 0;
    }
    free(sigs);

    for (int i = 0; i < program->proc_count; i++) {
        ProcDecl* proc = &program->procs[i];
        int idx = find_proc(compiler, proc->name);
        compiler->procs[idx].offset = compiler->chunk->count;
        for (int p = 0; p < proc->param_count; p++) {
            add_local(compiler, proc->params[p].name, (int)strlen(proc->params[p].name), proc->params[p].type);
        }
        compile_block(compiler, proc->body);
        if (compiler->had_error) {
            if (error != NULL && error_size > 0) {
                strncpy(error, compiler->error_message, error_size - 1);
                error[error_size - 1] = '\0';
            }
            free_program(program);
            return 0;
        }
        emit_byte(compiler, OP_RETURN);
        compiler->local_count = 0;
        compiler->scope_depth = 0;
    }

    free_program(program);
    return 1;
}

static int compiler_compile_source(Compiler* compiler, const char* source, int is_main, const char* path, char* error, size_t error_size) {
    char* saved = compiler->current_path;
    char* dir = NULL;
    if (path != NULL) {
        dir = path_directory(path);
        compiler->current_path = dir;
    }
    int ok = do_compile_source(compiler, source, is_main, error, error_size);
    if (dir != NULL) free(dir);
    compiler->current_path = saved;
    return ok;
}

int compile_with_context_and_path(const char* source, Chunk* chunk, const char* path, char* error, size_t error_size, struct Context* ctx) {
    Compiler compiler;
    compiler.chunk = chunk;
    compiler.local_count = 0;
    compiler.scope_depth = 0;
    compiler.proc_count = 0;
    compiler.patch_count = 0;
    compiler.had_error = 0;
    compiler.error_message[0] = '\0';
    compiler.entry_index = -1;
    compiler.loaded_count = 0;
    compiler.loading_count = 0;
    compiler.current_path = NULL;
    compiler.ctx = ctx;
    compiler.loop_count = 0;

    if (chunk->count == 0) {
        emit_byte(&compiler, OP_CALL);
        emit_u16(&compiler, 0);
        emit_byte(&compiler, 0); /* arg count */
        emit_byte(&compiler, OP_RETURN);
        compiler.entry_patch = chunk->count - 4;
    } else {
        compiler.entry_patch = -1;
    }

    if (!compiler_compile_source(&compiler, source, 1, path, error, error_size)) {
        free_proc_entries(&compiler);
        for (int i = 0; i < compiler.patch_count; i++) free((void*)compiler.patches[i].name);
        for (int i = 0; i < compiler.loaded_count; i++) free(compiler.loaded_paths[i]);
        return 0;
    }

    if (compiler.entry_index < 0) {
        if (error != NULL && error_size > 0) {
            strncpy(error, "No main procedure", error_size - 1);
            error[error_size - 1] = '\0';
        }
        free_proc_entries(&compiler);
        for (int i = 0; i < compiler.patch_count; i++) free((void*)compiler.patches[i].name);
        for (int i = 0; i < compiler.loaded_count; i++) free(compiler.loaded_paths[i]);
        return 0;
    }

    patch_call(chunk, compiler.entry_patch, compiler.procs[compiler.entry_index].offset);

    for (int i = 0; i < compiler.patch_count; i++) {
        int idx = find_proc(&compiler, compiler.patches[i].name);
        if (idx < 0) {
            if (error != NULL && error_size > 0) {
                snprintf(error, error_size, "Undefined procedure '%s'", compiler.patches[i].name);
            }
            free_proc_entries(&compiler);
            for (int j = 0; j < compiler.patch_count; j++) free((void*)compiler.patches[j].name);
            for (int j = 0; j < compiler.loaded_count; j++) free(compiler.loaded_paths[j]);
            return 0;
        }
        patch_call(chunk, compiler.patches[i].offset, compiler.procs[idx].offset);
    }

    free_proc_entries(&compiler);
    for (int i = 0; i < compiler.patch_count; i++) free((void*)compiler.patches[i].name);
    for (int i = 0; i < compiler.loaded_count; i++) free(compiler.loaded_paths[i]);
    return 1;
}

int compile_with_context(const char* source, Chunk* chunk, char* error, size_t error_size, struct Context* ctx) {
    return compile_with_context_and_path(source, chunk, NULL, error, error_size, ctx);
}

int compile_with_path(const char* source, Chunk* chunk, const char* path, char* error, size_t error_size) {
    return compile_with_context_and_path(source, chunk, path, error, error_size, NULL);
}

int compile(const char* source, Chunk* chunk, char* error, size_t error_size) {
    return compile_with_context(source, chunk, error, error_size, NULL);
}
