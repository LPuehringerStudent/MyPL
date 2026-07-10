#include <libgen.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "compiler.h"
#include "diagnostics.h"
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
    int continue_patches[MAX_BREAK_PATCHES];
    int continue_count;
} LoopContext;

typedef struct {
    const char* name;
    int offset;
    Type* return_type;
    Type** param_types;
    ParamMode* param_modes;
    int param_count;
} ProcEntry;

typedef struct {
    int offset;
    const char* name;
} CallPatch;

typedef struct {
    const char* name;
    int length;
    const char* query;
} CursorQuery;

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
    const char* source_path;
    struct Context* ctx;
    LoopContext loops[MAX_LOOP_NESTING];
    int loop_count;
    int current_line;
    int current_column;
    CursorQuery cursor_queries[MAX_LOCALS];
    int cursor_query_count;
} Compiler;

static int add_proc_entry(Compiler* compiler, const char* name, int offset,
                          Type* return_type, Type** param_types,
                          ParamMode* param_modes, int param_count) {
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
    ParamMode* pm_copy = NULL;
    if (param_count > 0) {
        pt_copy = malloc(sizeof(Type*) * (size_t)param_count);
        if (pt_copy == NULL) { free(name_copy); type_free(rt_copy); return -1; }
        pm_copy = malloc(sizeof(ParamMode) * (size_t)param_count);
        if (pm_copy == NULL) {
            free(pt_copy);
            free(name_copy);
            type_free(rt_copy);
            return -1;
        }
        for (int i = 0; i < param_count; i++) {
            pt_copy[i] = type_copy(param_types[i]);
            if (pt_copy[i] == NULL && param_types[i] != NULL) {
                for (int j = 0; j < i; j++) type_free(pt_copy[j]);
                free(pt_copy);
                free(pm_copy);
                free(name_copy);
                type_free(rt_copy);
                return -1;
            }
            pm_copy[i] = param_modes[i];
        }
    }
    compiler->procs[compiler->proc_count].name = name_copy;
    compiler->procs[compiler->proc_count].offset = offset;
    compiler->procs[compiler->proc_count].return_type = rt_copy;
    compiler->procs[compiler->proc_count].param_types = pt_copy;
    compiler->procs[compiler->proc_count].param_modes = pm_copy;
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
        free(compiler->procs[i].param_modes);
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
    format_error(compiler->error_message, sizeof(compiler->error_message),
                 compiler->source_path, compiler->current_line,
                 compiler->current_column, message);
}

static void compiler_format_error(Compiler* compiler, char* error, size_t error_size,
                                  const char* fmt, ...) {
    if (error == NULL || error_size == 0) return;
    char msg[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    format_error(error, error_size, compiler->source_path,
                 compiler->current_line, compiler->current_column, msg);
}

static void emit_byte(Compiler* compiler, uint8_t byte) {
    write_chunk_line(compiler->chunk, byte, compiler->current_line, compiler->current_column);
}

static void emit_u16(Compiler* compiler, uint16_t value) {
    write_chunk_u16_line(compiler->chunk, value, compiler->current_line, compiler->current_column);
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
    loop->continue_count = 0;
    return 1;
}

static void pop_loop(Compiler* compiler) {
    LoopContext* loop = &compiler->loops[compiler->loop_count - 1];
    int target = compiler->chunk->count;
    for (int i = 0; i < loop->break_count; i++) {
        patch_jump_to(compiler, loop->break_patches[i], target);
    }
    for (int i = 0; i < loop->continue_count; i++) {
        patch_jump_to(compiler, loop->continue_patches[i], loop->continue_target);
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
    if (loop->continue_count >= MAX_BREAK_PATCHES) {
        error(compiler, "too many continue statements in loop");
        return;
    }
    int pops = compiler->local_count - loop->continue_local_count;
    for (int i = 0; i < pops; i++) {
        emit_byte(compiler, OP_POP);
    }
    loop->continue_patches[loop->continue_count++] = emit_jump(compiler, OP_JMP);
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

static void register_cursor_query(Compiler* compiler, const char* name, int length, const char* query) {
    for (int i = 0; i < compiler->cursor_query_count; i++) {
        if (compiler->cursor_queries[i].length == length &&
            memcmp(compiler->cursor_queries[i].name, name, (size_t)length) == 0) {
            compiler->cursor_queries[i].query = query;
            return;
        }
    }
    if (compiler->cursor_query_count >= MAX_LOCALS) return;
    compiler->cursor_queries[compiler->cursor_query_count].name = name;
    compiler->cursor_queries[compiler->cursor_query_count].length = length;
    compiler->cursor_queries[compiler->cursor_query_count].query = query;
    compiler->cursor_query_count++;
}

static const char* find_cursor_query(Compiler* compiler, const char* name, int length) {
    for (int i = 0; i < compiler->cursor_query_count; i++) {
        if (compiler->cursor_queries[i].length == length &&
            memcmp(compiler->cursor_queries[i].name, name, (size_t)length) == 0) {
            return compiler->cursor_queries[i].query;
        }
    }
    return NULL;
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
        compiler->current_column = expr->loc.column;
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
        case EXPR_MAP_LITERAL: {
            MapLiteralExpr* m = &expr->as.map_literal;
            for (int i = 0; i < m->count; i++) {
                compile_expr(compiler, m->values[i]);
                if (compiler->had_error) return;
                compile_expr(compiler, m->keys[i]);
                if (compiler->had_error) return;
            }
            emit_byte(compiler, OP_MAP_BUILD);
            emit_u16(compiler, (uint16_t)m->count);
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
                int proc_idx = find_proc(compiler, c->name);
                ParamMode* modes = NULL;
                if (proc_idx >= 0) {
                    modes = compiler->procs[proc_idx].param_modes;
                }

                int out_count = 0;
                uint8_t out_slots[64];
                int out_positions[64];
                for (int i = 0; i < c->arg_count; i++) {
                    ParamMode mode = (modes != NULL && i < compiler->procs[proc_idx].param_count)
                                         ? modes[i]
                                         : PARAM_IN;
                    if (mode == PARAM_OUT) {
                        emit_constant(compiler, value_int(0));
                    } else {
                        compile_expr(compiler, c->args[i]);
                        if (compiler->had_error) return;
                    }
                    if (mode == PARAM_OUT || mode == PARAM_INOUT) {
                        if (c->args[i]->kind != EXPR_VARIABLE) {
                            error(compiler, "OUT/IN OUT argument must be a variable");
                            return;
                        }
                        if (out_count >= 64) {
                            error(compiler, "too many OUT/IN OUT arguments");
                            return;
                        }
                        const char* var_name = c->args[i]->as.variable.name;
                        int slot = resolve_local(compiler, var_name, (int)strlen(var_name));
                        if (slot < 0) {
                            char msg[256];
                            snprintf(msg, sizeof(msg), "Undefined variable '%s'", var_name);
                            error(compiler, msg);
                            return;
                        }
                        out_positions[out_count] = i;
                        out_slots[out_count++] = (uint8_t)slot;
                    }
                }

                if (out_count > 0) {
                    emit_byte(compiler, OP_CALL_OUT);
                    if (proc_idx >= 0 && compiler->procs[proc_idx].offset >= 0) {
                        emit_u16(compiler, (uint16_t)compiler->procs[proc_idx].offset);
                    } else {
                        if (!add_call_patch(compiler, c->name, compiler->chunk->count)) {
                            error(compiler, "too many call patches");
                            return;
                        }
                        emit_u16(compiler, 0);
                    }
                    emit_byte(compiler, (uint8_t)c->arg_count);
                    emit_byte(compiler, (uint8_t)out_count);
                    for (int i = 0; i < out_count; i++) {
                        emit_byte(compiler, (uint8_t)out_positions[i]);
                        emit_byte(compiler, out_slots[i]);
                    }
                } else {
                    emit_call(compiler, c->name, c->arg_count);
                }
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
        case EXPR_CURSOR_ATTR: {
            const char* cursor_name = expr->as.cursor_attr.cursor_name;
            int slot = resolve_local(compiler, cursor_name, (int)strlen(cursor_name));
            if (slot < 0) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Undefined variable '%s'", cursor_name);
                error(compiler, msg);
                return;
            }
            char* attr_name = malloc((size_t)strlen(expr->as.cursor_attr.attr_name) + 1);
            if (attr_name == NULL) {
                error(compiler, "out of memory");
                return;
            }
            strcpy(attr_name, expr->as.cursor_attr.attr_name);
            int attr_idx = add_constant(compiler->chunk, value_string(attr_name));
            if (attr_idx < 0) {
                free(attr_name);
                error(compiler, "too many constants");
                return;
            }
            emit_byte(compiler, OP_GET_LOCAL);
            emit_byte(compiler, (uint8_t)slot);
            emit_byte(compiler, OP_CURSOR_ATTR);
            emit_u16(compiler, (uint16_t)attr_idx);
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
        case EXPR_STRUCT_LITERAL: {
            StructLiteralExpr* sl = &expr->as.struct_literal;
            for (int i = 0; i < sl->field_count; i++) {
                compile_expr(compiler, sl->values[i]);
                if (compiler->had_error) return;
            }
            size_t schema_len = 0;
            for (int i = 0; i < sl->field_count; i++) {
                if (i > 0) schema_len++;
                schema_len += strlen(sl->field_names[i]);
            }
            char* schema = malloc(schema_len + 1);
            if (schema == NULL) {
                error(compiler, "out of memory");
                return;
            }
            schema[0] = '\0';
            for (int i = 0; i < sl->field_count; i++) {
                if (i > 0) strcat(schema, ",");
                strcat(schema, sl->field_names[i]);
            }
            int schema_idx = add_constant(compiler->chunk, value_string(schema));
            if (schema_idx < 0) {
                free(schema);
                error(compiler, "too many constants");
                return;
            }
            emit_byte(compiler, OP_STRUCT_BUILD);
            emit_u16(compiler, (uint16_t)schema_idx);
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
        compiler->current_column = stmt->loc.column;
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
            int start_count = compiler->local_count;
            if (w->is_do_while) {
                int body_start = compiler->chunk->count;
                if (!push_loop(compiler, body_start, start_count, start_count)) return;
                compile_block(compiler, w->body);
                if (compiler->had_error) return;
                compile_expr(compiler, w->condition);
                if (compiler->had_error) return;
                int exit_jump = emit_jump(compiler, OP_JZ);
                int back = emit_jump(compiler, OP_JMP);
                patch_jump_to(compiler, back, body_start);
                patch_jump(compiler, exit_jump);
                pop_loop(compiler);
            } else {
                int loop_start = compiler->chunk->count;
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
            }
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
        case STMT_FOR_C: {
            CForStmt* cf = &stmt->as.cfor_stmt;
            int start_count = compiler->local_count;

            if (cf->init != NULL) {
                compile_stmt(compiler, cf->init);
                if (compiler->had_error) return;
            }

            int loop_start = compiler->chunk->count;

            if (cf->condition != NULL) {
                compile_expr(compiler, cf->condition);
                if (compiler->had_error) return;
            } else {
                emit_constant(compiler, value_bool(1));
            }
            int exit_jump = emit_jump(compiler, OP_JZ);

            /* Push loop before body so break/continue are captured; continue
               target will be set to the step clause once the body is emitted. */
            if (!push_loop(compiler, loop_start, start_count, start_count)) return;

            compile_block(compiler, cf->body);
            if (compiler->had_error) return;

            compiler->loops[compiler->loop_count - 1].continue_target = compiler->chunk->count;

            if (cf->step != NULL) {
                compile_stmt(compiler, cf->step);
                if (compiler->had_error) return;
            }

            int back = emit_jump(compiler, OP_JMP);
            patch_jump_to(compiler, back, loop_start);
            patch_jump(compiler, exit_jump);
            pop_loop(compiler);

            compiler->local_count = start_count;
            break;
        }
        case STMT_FOR: {
            ForStmt* f = &stmt->as.for_stmt;
            int start_count = compiler->local_count;

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
            if (!push_loop(compiler, loop_start, start_count, start_count + 1)) return;
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
            pop_loop(compiler);
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
        case STMT_TRY_CATCH: {
            TryCatchStmt* tc = &stmt->as.try_catch;
            int catch_slot = add_local(compiler, tc->catch_var, (int)strlen(tc->catch_var), &type_string);
            if (catch_slot < 0) {
                error(compiler, "too many locals");
                return;
            }
            int local_count_at_try = compiler->local_count;

            emit_byte(compiler, OP_TRY);
            int catch_offset = compiler->chunk->count;
            emit_u16(compiler, 0);
            emit_byte(compiler, (uint8_t)local_count_at_try);
            int after_try_operands = compiler->chunk->count;

            compile_block(compiler, tc->try_block);
            if (compiler->had_error) return;

            emit_byte(compiler, OP_END_TRY);
            int end_offset = compiler->chunk->count;
            emit_u16(compiler, 0);

            int catch_start = compiler->chunk->count;
            int catch_jump = catch_start - after_try_operands;
            compiler->chunk->code[catch_offset] = (uint8_t)((catch_jump >> 8) & 0xFF);
            compiler->chunk->code[catch_offset + 1] = (uint8_t)(catch_jump & 0xFF);

            /* vm_catch already pushed the error message at frame_base[catch_slot]
             * and set stack_top to catch_slot + 1, so the message is both the
             * initial value of the catch variable and the bottom of the stack.
             * Emitting SET_LOCAL + POP here would release the same object twice
             * because the source and destination are the same slot. */
            (void)catch_slot;

            compile_block(compiler, tc->catch_block);
            if (compiler->had_error) return;

            emit_byte(compiler, OP_POP);
            compiler->local_count--;

            int after_catch = compiler->chunk->count;
            patch_jump_to(compiler, end_offset, after_catch);
            break;
        }
        case STMT_CURSOR_DECL: {
            CursorDeclStmt* d = &stmt->as.cursor_decl;
            int slot = add_local(compiler, d->name, (int)strlen(d->name), &type_cursor);
            if (slot < 0) {
                error(compiler, "too many locals");
                return;
            }
            /* Initialize cursor slot to a closed cursor object. */
            emit_constant(compiler, value_cursor(NULL));
            emit_byte(compiler, OP_SET_LOCAL);
            emit_byte(compiler, (uint8_t)slot);
            if (d->sql_query != NULL) {
                register_cursor_query(compiler, d->name, (int)strlen(d->name), d->sql_query);
            }
            break;
        }
        case STMT_CURSOR_OPEN: {
            CursorOpenStmt* o = &stmt->as.cursor_open;
            int slot = resolve_local(compiler, o->name, (int)strlen(o->name));
            if (slot < 0) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Undefined variable '%s'", o->name);
                error(compiler, msg);
                return;
            }
            const char* query = o->sql_query;
            if (query == NULL) {
                query = find_cursor_query(compiler, o->name, (int)strlen(o->name));
                if (query == NULL) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "Cursor '%s' has no associated query", o->name);
                    error(compiler, msg);
                    return;
                }
            }
            for (int i = 0; i < o->param_count; i++) {
                compile_expr(compiler, o->params[i]);
                if (compiler->had_error) return;
                const char* name = o->params[i]->as.sql_param.name;
                Type* t = resolve_local_type(compiler, name, (int)strlen(name));
                if (t != NULL && t->kind == TYPE_STRING) {
                    emit_byte(compiler, OP_SQL_BIND_STRING);
                } else if (t != NULL && t->kind == TYPE_FLOAT) {
                    emit_byte(compiler, OP_SQL_BIND_FLOAT);
                } else {
                    emit_byte(compiler, OP_SQL_BIND_INT);
                }
            }
            char* query_copy = malloc((size_t)strlen(query) + 1);
            if (query_copy == NULL) {
                error(compiler, "out of memory");
                return;
            }
            strcpy(query_copy, query);
            int query_idx = add_constant(compiler->chunk, value_string(query_copy));
            if (query_idx < 0) {
                free(query_copy);
                error(compiler, "too many constants");
                return;
            }
            emit_byte(compiler, OP_GET_LOCAL);
            emit_byte(compiler, (uint8_t)slot);
            emit_byte(compiler, OP_CURSOR_OPEN);
            emit_u16(compiler, (uint16_t)query_idx);
            emit_u16(compiler, (uint16_t)stmt->loc.line);
            emit_byte(compiler, OP_SET_LOCAL);
            emit_byte(compiler, (uint8_t)slot);
            emit_byte(compiler, OP_POP);
            break;
        }
        case STMT_CURSOR_FETCH: {
            CursorFetchStmt* f = &stmt->as.cursor_fetch;
            int slot = resolve_local(compiler, f->name, (int)strlen(f->name));
            if (slot < 0) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Undefined variable '%s'", f->name);
                error(compiler, msg);
                return;
            }
            uint8_t into_slots[64];
            if (f->into_count > 64) {
                error(compiler, "too many fetch targets");
                return;
            }
            for (int i = 0; i < f->into_count; i++) {
                int var_slot = resolve_local(compiler, f->into_vars[i], (int)strlen(f->into_vars[i]));
                if (var_slot < 0) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "Undefined variable '%s'", f->into_vars[i]);
                    error(compiler, msg);
                    return;
                }
                into_slots[i] = (uint8_t)var_slot;
            }
            emit_byte(compiler, OP_GET_LOCAL);
            emit_byte(compiler, (uint8_t)slot);
            emit_byte(compiler, OP_CURSOR_FETCH);
            emit_byte(compiler, (uint8_t)f->into_count);
            for (int i = 0; i < f->into_count; i++) {
                emit_byte(compiler, into_slots[i]);
            }
            break;
        }
        case STMT_CURSOR_CLOSE: {
            CursorCloseStmt* c = &stmt->as.cursor_close;
            int slot = resolve_local(compiler, c->name, (int)strlen(c->name));
            if (slot < 0) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Undefined variable '%s'", c->name);
                error(compiler, msg);
                return;
            }
            emit_byte(compiler, OP_GET_LOCAL);
            emit_byte(compiler, (uint8_t)slot);
            emit_byte(compiler, OP_CURSOR_CLOSE);
            break;
        }
        case STMT_CASE: {
            CaseStmt* cs = &stmt->as.case_stmt;
            compile_expr(compiler, cs->selector);
            if (compiler->had_error) return;

            int* end_jumps = malloc(sizeof(int) * (size_t)(cs->branch_count + 1));
            if (end_jumps == NULL) {
                error(compiler, "out of memory");
                return;
            }
            int end_count = 0;

            for (int i = 0; i < cs->branch_count; i++) {
                emit_byte(compiler, OP_DUP);
                compile_expr(compiler, cs->values[i]);
                if (compiler->had_error) {
                    free(end_jumps);
                    return;
                }
                emit_byte(compiler, OP_EQ);
                int skip_branch = emit_jump(compiler, OP_JZ);

                compile_block(compiler, cs->blocks[i]);
                if (compiler->had_error) {
                    free(end_jumps);
                    return;
                }
                end_jumps[end_count++] = emit_jump(compiler, OP_JMP);

                patch_jump_to(compiler, skip_branch, compiler->chunk->count);
            }

            if (cs->else_block != NULL) {
                compile_block(compiler, cs->else_block);
                if (compiler->had_error) {
                    free(end_jumps);
                    return;
                }
            }

            int after_case = compiler->chunk->count;
            for (int i = 0; i < end_count; i++) {
                patch_jump_to(compiler, end_jumps[i], after_case);
            }
            emit_byte(compiler, OP_POP); /* pop selector */
            free(end_jumps);
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
        compiler_format_error(compiler, error, error_size,
                              "Module not found: %s", import_path);
        return 0;
    }

    if (is_module_loading(compiler, resolved)) {
        free(resolved);
        compiler_format_error(compiler, error, error_size,
                              "Circular import of '%s'", import_path);
        return 0;
    }
    if (is_module_loaded(compiler, resolved)) {
        free(resolved);
        return 1;
    }
    if (compiler->loaded_count >= MAX_MODULES) {
        free(resolved);
        compiler_format_error(compiler, error, error_size,
                              "Too many imported modules");
        return 0;
    }
    if (!os_file_exists(resolved)) {
        free(resolved);
        compiler_format_error(compiler, error, error_size,
                              "Module not found: %s", import_path);
        return 0;
    }

    char* source = os_read_file(resolved);
    if (source == NULL) {
        free(resolved);
        compiler_format_error(compiler, error, error_size,
                              "Could not read module: %s", import_path);
        return 0;
    }

    if (compiler->loading_count >= MAX_LOADING) {
        free(resolved);
        free(source);
        compiler_format_error(compiler, error, error_size,
                              "Too many nested imports");
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
    Program* program = parse_with_path(source, compiler->source_path, parse_error, sizeof(parse_error));
    if (program == NULL) {
        if (error != NULL && error_size > 0) {
            strncpy(error, parse_error, error_size - 1);
            error[error_size - 1] = '\0';
        }
        return 0;
    }

    for (int i = 0; i < program->import_count; i++) {
        Stmt* import_stmt = program->imports[i];
        const char* path = import_stmt->as.import_stmt.path;
        compiler->current_line = import_stmt->loc.line;
        compiler->current_column = import_stmt->loc.column;
        if (!compiler_load_module(compiler, path, error, error_size)) {
            free_program(program);
            return 0;
        }
    }

    for (int i = 0; i < program->proc_count; i++) {
        ProcDecl* proc = &program->procs[i];
        Type** pts = NULL;
        ParamMode* pms = NULL;
        if (proc->param_count > 0) {
            pts = malloc(sizeof(Type*) * (size_t)proc->param_count);
            pms = malloc(sizeof(ParamMode) * (size_t)proc->param_count);
            for (int p = 0; p < proc->param_count; p++) {
                pts[p] = proc->params[p].type;
                pms[p] = proc->params[p].mode;
            }
        }
        int idx = add_proc_entry(compiler, proc->name, -1, proc->return_type, pts, pms, proc->param_count);
        free(pts);
        free(pms);
        if (idx < 0) {
            if (error != NULL && error_size > 0) {
                if (idx == -1) {
                    compiler_format_error(compiler, error, error_size,
                                          "Out of memory registering procedure '%s'", proc->name);
                } else if (idx == -2) {
                    compiler_format_error(compiler, error, error_size,
                                          "Duplicate procedure '%s'", proc->name);
                } else {
                    compiler_format_error(compiler, error, error_size,
                                          "Too many procedures");
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
        compiler_format_error(compiler, error, error_size, "out of memory");
        free_program(program);
        return 0;
    }
    for (int i = 0; i < compiler->proc_count; i++) {
        sigs[i].name = compiler->procs[i].name;
        sigs[i].return_type = compiler->procs[i].return_type;
        sigs[i].param_types = compiler->procs[i].param_types;
        sigs[i].param_modes = compiler->procs[i].param_modes;
        sigs[i].param_count = compiler->procs[i].param_count;
    }

    if (!typecheck_program(program, sigs, compiler->proc_count, compiler->ctx,
                           compiler->source_path, error, error_size)) {
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
    const char* saved_source = compiler->source_path;
    char* dir = NULL;
    if (path != NULL) {
        dir = path_directory(path);
        compiler->current_path = dir;
        compiler->source_path = path;
    }
    int ok = do_compile_source(compiler, source, is_main, error, error_size);
    if (dir != NULL) free(dir);
    compiler->current_path = saved;
    compiler->source_path = saved_source;
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
    compiler.source_path = path;
    compiler.ctx = ctx;
    compiler.loop_count = 0;
    compiler.current_line = 0;
    compiler.current_column = 0;
    chunk->source_path = path;

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
            format_error(error, error_size, compiler.source_path, 0, 0,
                         "No main procedure");
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
                char msg[256];
                snprintf(msg, sizeof(msg), "Undefined procedure '%s'", compiler.patches[i].name);
                format_error(error, error_size, compiler.source_path, 0, 0, msg);
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
