#include "test_harness.h"
#include "compiler.h"
#include "vm.h"
#include "sql_engine.h"

#include <string.h>
#include <unistd.h>

static char s_module_path[256] = {0};
static char s_bad_module_path[256] = {0};
static char s_mid_module_path[256] = {0};

static void cleanup_temp_modules(void) {
    if (s_module_path[0] != '\0') remove(s_module_path);
    if (s_bad_module_path[0] != '\0') remove(s_bad_module_path);
    if (s_mid_module_path[0] != '\0') remove(s_mid_module_path);
}

TEST(compiler_compiles_integer_return) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { return 42; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(42, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_local_variables) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { int x = 10; x = 20; return x; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(20, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_arithmetic) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { return 1 + 2 * 3; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(7, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_comparison) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> bool { return 5 == 5; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(1, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_if_statement) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { if 0 { return 13; } return 42; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(42, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_if_true_branch) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { if 1 { return 13; } return 42; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(13, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_else_branch) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { if 0 { return 13; } else { return 42; } }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(42, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_else_if_chain) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { if 0 { return 1; } else if 0 { return 2; } else { return 3; } }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(3, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_while_loop) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { int i = 0; int s = 0; while i < 5 { s = s + i; i = i + 1; } return s; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(10, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_while_break) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { int i = 0; while i < 10 { i = i + 1; if i == 3 { break; } } return i; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(3, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_while_continue) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { int i = 0; int s = 0; while i < 5 { i = i + 1; if i == 3 { continue; } s = s + i; } return s; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(12, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_foreach_range) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { int s = 0; for i in range(0, 5) { s = s + i; } return s; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(10, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_foreach_array) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { int s = 0; array<int> a = [2, 3, 4]; for i in a { s = s + i; } return s; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(9, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_parse_int_native) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { return parse_int(\"123\"); }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(123, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_split_lines_and_join_paths) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { array<string> lines = split_lines(\"1\\n2\\n3\"); return length(lines) + parse_int(lines[0]); }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(4, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_unary_minus) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { return -7; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(-7, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_unary_not) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> bool { return !0; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(1, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_unary_not_true) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> bool { return !1; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(0, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_float_return) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> float { return 3.14; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_FLOAT_EQ(3.14, vm_pop(vm).as.as_float);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_float_arithmetic) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> float { return 1.5 + 2.5; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_FLOAT_EQ(4.0, vm_pop(vm).as.as_float);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_mixed_int_float_arithmetic) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> float { return 1 + 2.5; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_FLOAT_EQ(3.5, vm_pop(vm).as.as_float);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_string_return) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> string { return \"hello\"; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    Value result = vm_pop(vm);
    ASSERT_INT_EQ(VAL_STRING, result.type);
    ASSERT_STRING_EQ("hello", result.as.as_string);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_procedure_call) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc seven() -> int { return 7; } proc main() -> int { return seven(); }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(7, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_forward_procedure_call) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { return later(); } proc later() -> int { return 9; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(9, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_procedure_with_parameters) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc add(a int, b int) -> int { return a + b; } proc main() -> int { return add(3, 4); }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(7, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_procedure_with_parameter_and_local) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc f(x int) -> int { int y = 10; return x + y; } proc main() -> int { return f(5); }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(15, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_forward_call_with_arguments) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { return later(2, 3); } proc later(a int, b int) -> int { return a * b; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(6, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_for_sql_loop) {
    char path[] = "/tmp/mydb_test_compiler_1_XXXXXX.db";
    int fd = mkstemp(path);
    if (fd >= 0) close(fd);
    unlink(path);

    Context ctx = {path, NULL};
    ASSERT_INT_EQ(1, catalog_open(&ctx));
    const char* cols[] = {"id"};
    int types[] = {VAL_INT};
    Table* t = catalog_create_table(&ctx, "users", cols, types, 1);
    ASSERT_PTR_NOT_NULL(t);

    Cell cells[1];
    cells[0].type = VAL_INT;
    cells[0].as.as_int = 1;
    catalog_insert(&ctx, t, cells);

    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { for row in SELECT id FROM users { return row.id; } return 0; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    vm_set_context(vm, &ctx);
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(1, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
    catalog_close(&ctx);
    unlink(path);
}

TEST(compiler_compiles_for_sql_loop_sum) {
    char path[] = "/tmp/mydb_test_compiler_2_XXXXXX.db";
    int fd = mkstemp(path);
    if (fd >= 0) close(fd);
    unlink(path);

    Context ctx = {path, NULL};
    ASSERT_INT_EQ(1, catalog_open(&ctx));
    const char* cols[] = {"id"};
    int types[] = {VAL_INT};
    Table* t = catalog_create_table(&ctx, "users", cols, types, 1);
    ASSERT_PTR_NOT_NULL(t);

    Cell cells[1];
    for (int i = 1; i <= 3; i++) {
        cells[0].type = VAL_INT;
        cells[0].as.as_int = i;
        catalog_insert(&ctx, t, cells);
    }

    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { int sum = 0; for row in SELECT id FROM users { sum = sum + row.id; } return sum; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    vm_set_context(vm, &ctx);
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(6, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
    catalog_close(&ctx);
    unlink(path);
}

TEST(compiler_block_scope_does_not_leak_locals) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { if 1 { int x = 10; } int y = 5; return y; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(5, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_reports_undefined_variable) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(0, compile("proc main() -> int { return unknown; }", &chunk, NULL, 0));
    free_chunk(&chunk);
}

TEST(compiler_returns_error_message_for_undefined_variable) {
    Chunk chunk;
    init_chunk(&chunk);
    char error[256];
    ASSERT_INT_EQ(0, compile("proc main() -> int { return unknown; }", &chunk, error, sizeof(error)));
    if (strstr(error, "Undefined variable 'unknown'") == NULL) {
        FAIL("expected undefined variable error");
    }
    free_chunk(&chunk);
}

TEST(compiler_compiles_print_statement) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { print(42); return 0; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_print_string_statement) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { print(\"hello\"); return 0; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_bool_literal) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> bool { return true; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(1, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_array_literal_and_index) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { array a = [10, 20, 30]; return a[1]; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(20, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_index_assignment) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { array a = [1, 2]; a[0] = 99; return a[0]; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(99, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_array_length) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { array a = [1, 2, 3]; return length(a); }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(3, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_array_append) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { array a = [1, 2]; a = append(a, 3); return length(a); }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(3, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_println) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { println(42); return 0; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(0, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_clock) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { int t = clock(); return t; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    Value result = vm_pop(vm);
    ASSERT_INT_EQ(VAL_INT, result.type);
    ASSERT_INT_EQ(1, result.as.as_int >= 0);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_imported_procedure) {
    static int cleanup_registered = 0;
    if (!cleanup_registered) {
        atexit(cleanup_temp_modules);
        cleanup_registered = 1;
    }

    snprintf(s_module_path, sizeof(s_module_path), "/tmp/mypl_test_module_XXXXXX");
    int fd = mkstemp(s_module_path);
    ASSERT_INT_EQ(1, fd >= 0);
    close(fd);

    FILE* f = fopen(s_module_path, "w");
    ASSERT_PTR_NOT_NULL(f);
    fprintf(f, "proc double(n int) -> int { return n * 2; }\n");
    fclose(f);

    char source[512];
    snprintf(source, sizeof(source),
             "import \"%s\"; proc main() -> int { return double(21); }",
             s_module_path);

    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile(source, &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(42, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
    remove(s_module_path);
    s_module_path[0] = '\0';
}

TEST(compiler_reports_original_error_for_bad_import) {
    static int cleanup_registered = 0;
    if (!cleanup_registered) {
        atexit(cleanup_temp_modules);
        cleanup_registered = 1;
    }

    snprintf(s_bad_module_path, sizeof(s_bad_module_path), "/tmp/mypl_test_bad_XXXXXX");
    int fd = mkstemp(s_bad_module_path);
    ASSERT_INT_EQ(1, fd >= 0);
    close(fd);

    FILE* f = fopen(s_bad_module_path, "w");
    ASSERT_PTR_NOT_NULL(f);
    fprintf(f, "proc broken() -> int { return 1;\n");
    fclose(f);

    snprintf(s_mid_module_path, sizeof(s_mid_module_path), "/tmp/mypl_test_mid_XXXXXX");
    fd = mkstemp(s_mid_module_path);
    ASSERT_INT_EQ(1, fd >= 0);
    close(fd);

    f = fopen(s_mid_module_path, "w");
    ASSERT_PTR_NOT_NULL(f);
    fprintf(f, "import \"%s\";\n", s_bad_module_path);
    fclose(f);

    char source[1024];
    snprintf(source, sizeof(source),
             "import \"%s\";\nimport \"%s\";\nproc main() -> int { return 0; }\n",
             s_mid_module_path, s_bad_module_path);

    Chunk chunk;
    init_chunk(&chunk);
    char error[256];
    ASSERT_INT_EQ(0, compile(source, &chunk, error, sizeof(error)));
    if (strstr(error, "expected '}'") == NULL) {
        FAIL("expected parse error from bad module");
    }
    free_chunk(&chunk);
    remove(s_bad_module_path);
    remove(s_mid_module_path);
    s_bad_module_path[0] = '\0';
    s_mid_module_path[0] = '\0';
}

TEST(compiler_rejects_too_many_imports) {
    char source[4096];
    size_t pos = 0;
    for (int i = 0; i < 65; i++) {
        pos += (size_t)snprintf(source + pos, sizeof(source) - pos,
                                "import \"m%d.mypl\";\n", i);
    }
    pos += (size_t)snprintf(source + pos, sizeof(source) - pos,
                            "proc main() -> int { return 0; }\n");

    Chunk chunk;
    init_chunk(&chunk);
    char error[256];
    ASSERT_INT_EQ(0, compile(source, &chunk, error, sizeof(error)));
    if (strstr(error, "too many imports") == NULL) {
        FAIL("expected too many imports error");
    }
    free_chunk(&chunk);
}

TEST(compiler_rejects_type_mismatch_in_assignment) {
    Chunk chunk;
    init_chunk(&chunk);
    char error[256];
    ASSERT_INT_EQ(0, compile("proc main() -> int { int x = \"hello\"; return 0; }", &chunk, error, sizeof(error)));
    if (strstr(error, "Type error") == NULL || strstr(error, "string") == NULL) {
        FAIL("expected type mismatch error message");
    }
    free_chunk(&chunk);
}

TEST(compiler_accepts_typed_array_program) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { array<int> a = [1, 2]; return a[0]; }", &chunk, NULL, 0));
    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(1, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_typechecks_sql_row_field_with_context) {
    char path[] = "/tmp/mypl_test_compiler_ctx_XXXXXX.db";
    int fd = mkstemp(path);
    if (fd >= 0) close(fd);
    unlink(path);

    Context ctx;
    ctx.db_path = path;
    ctx.pager = NULL;
    ASSERT_INT_EQ(1, catalog_open(&ctx));
    const char* cols[] = {"id"};
    int types[] = {VAL_INT};
    Table* t = catalog_create_table(&ctx, "users", cols, types, 1);
    ASSERT_PTR_NOT_NULL(t);

    Chunk chunk;
    init_chunk(&chunk);
    char error[256];
    ASSERT_INT_EQ(1, compile_with_context(
        "proc main() -> int { for row in SELECT id FROM users { return row.id; } return 0; }",
        &chunk, error, sizeof(error), &ctx));

    free_chunk(&chunk);
    catalog_close(&ctx);
    unlink(path);
}

TEST(compiler_compiles_string_concatenation) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> string { return \"a\" + \"b\"; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    Value result = vm_pop(vm);
    ASSERT_INT_EQ(VAL_STRING, result.type);
    ASSERT_STRING_EQ("ab", result.as.as_string);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_string_comparison) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> bool { return \"a\" < \"b\"; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    Value result = vm_pop(vm);
    ASSERT_INT_EQ(VAL_INT, result.type);
    ASSERT_INT_EQ(1, result.as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_string_length) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { return length(\"hello\"); }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    Value result = vm_pop(vm);
    ASSERT_INT_EQ(VAL_INT, result.type);
    ASSERT_INT_EQ(5, result.as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_concat_native) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> string { return concat(\"x\", \"y\"); }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    Value result = vm_pop(vm);
    ASSERT_INT_EQ(VAL_STRING, result.type);
    ASSERT_STRING_EQ("xy", result.as.as_string);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_substring_native) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> string { return substring(\"hello\", 1, 3); }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    Value result = vm_pop(vm);
    ASSERT_INT_EQ(VAL_STRING, result.type);
    ASSERT_STRING_EQ("ell", result.as.as_string);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_int_to_string_native) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> string { return int_to_string(42); }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    Value result = vm_pop(vm);
    ASSERT_INT_EQ(VAL_STRING, result.type);
    ASSERT_STRING_EQ("42", result.as.as_string);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_abs_int_native) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { return abs_int(-7); }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    Value result = vm_pop(vm);
    ASSERT_INT_EQ(VAL_INT, result.type);
    ASSERT_INT_EQ(7, result.as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_min_max_int_natives) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { return max_int(3, 8); }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    Value result = vm_pop(vm);
    ASSERT_INT_EQ(VAL_INT, result.type);
    ASSERT_INT_EQ(8, result.as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_abs_float_native) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> float { return abs_float(-2.5); }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    Value result = vm_pop(vm);
    ASSERT_INT_EQ(VAL_FLOAT, result.type);
    ASSERT_INT_EQ(1, result.as.as_float == 2.5);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_nested_index_assignment) {
    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { array<array<int>> a = [[1, 2], [3, 4]]; a[0][1] = 9; return a[0][1]; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    Value result = vm_pop(vm);
    ASSERT_INT_EQ(VAL_INT, result.type);
    ASSERT_INT_EQ(9, result.as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(compiler_compiles_select_into_single_value) {
    char path[] = "/tmp/mydb_test_compiler_3_XXXXXX.db";
    int fd = mkstemp(path);
    if (fd >= 0) close(fd);
    unlink(path);

    Context ctx = {path, NULL};
    ASSERT_INT_EQ(1, catalog_open(&ctx));
    const char* cols[] = {"id"};
    int types[] = {VAL_INT};
    Table* t = catalog_create_table(&ctx, "users", cols, types, 1);
    ASSERT_PTR_NOT_NULL(t);

    Cell cells[1];
    cells[0].type = VAL_INT;
    cells[0].as.as_int = 1;
    catalog_insert(&ctx, t, cells);

    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { int my_id = 0; SELECT id INTO my_id FROM users WHERE id = 1; return my_id; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    vm_set_context(vm, &ctx);
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(1, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
    catalog_close(&ctx);
    unlink(path);
}

TEST(compiler_compiles_select_into_multi_value) {
    char path[] = "/tmp/mydb_test_compiler_4_XXXXXX.db";
    int fd = mkstemp(path);
    if (fd >= 0) close(fd);
    unlink(path);

    Context ctx = {path, NULL};
    ASSERT_INT_EQ(1, catalog_open(&ctx));
    const char* cols[] = {"id", "name"};
    int types[] = {VAL_INT, VAL_STRING};
    Table* t = catalog_create_table(&ctx, "users", cols, types, 2);
    ASSERT_PTR_NOT_NULL(t);

    Cell cells[2];
    cells[0].type = VAL_INT;
    cells[0].as.as_int = 1;
    cells[1].type = VAL_STRING;
    cells[1].as.as_string = "alice";
    catalog_insert(&ctx, t, cells);

    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { int my_id = 0; string my_name = \"\"; SELECT id, name INTO my_id, my_name FROM users WHERE id = 1; return my_id; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    vm_set_context(vm, &ctx);
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(1, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
    catalog_close(&ctx);
    unlink(path);
}

TEST(compiler_compiles_select_into_array_row) {
    char path[] = "/tmp/mydb_test_compiler_5_XXXXXX.db";
    int fd = mkstemp(path);
    if (fd >= 0) close(fd);
    unlink(path);

    Context ctx = {path, NULL};
    ASSERT_INT_EQ(1, catalog_open(&ctx));
    const char* cols[] = {"id", "name"};
    int types[] = {VAL_INT, VAL_STRING};
    Table* t = catalog_create_table(&ctx, "users", cols, types, 2);
    ASSERT_PTR_NOT_NULL(t);

    Cell cells[2];
    cells[0].type = VAL_INT;
    cells[0].as.as_int = 1;
    cells[1].type = VAL_STRING;
    cells[1].as.as_string = "alice";
    catalog_insert(&ctx, t, cells);
    cells[0].as.as_int = 2;
    cells[1].as.as_string = "bob";
    catalog_insert(&ctx, t, cells);

    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { array<row> rows = []; SELECT * INTO rows FROM users; return length(rows) + rows[0].id; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    vm_set_context(vm, &ctx);
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(3, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
    catalog_close(&ctx);
    unlink(path);
}

TEST(compiler_rejects_select_into_no_row) {
    char path[] = "/tmp/mydb_test_compiler_6_XXXXXX.db";
    int fd = mkstemp(path);
    if (fd >= 0) close(fd);
    unlink(path);

    Context ctx = {path, NULL};
    ASSERT_INT_EQ(1, catalog_open(&ctx));
    const char* cols[] = {"id"};
    int types[] = {VAL_INT};
    Table* t = catalog_create_table(&ctx, "users", cols, types, 1);
    ASSERT_PTR_NOT_NULL(t);

    Chunk chunk;
    init_chunk(&chunk);
    ASSERT_INT_EQ(1, compile("proc main() -> int { int my_id = 0; SELECT id INTO my_id FROM users WHERE id = 999; return my_id; }", &chunk, NULL, 0));

    VM* vm = vm_init();
    vm_set_context(vm, &ctx);
    ASSERT_INT_EQ(INTERPRET_RUNTIME_ERROR, vm_interpret(vm, &chunk));
    vm_free(vm);
    free_chunk(&chunk);
    catalog_close(&ctx);
    unlink(path);
}

TEST(compiler_emits_sql_exec) {
    Chunk chunk;
    init_chunk(&chunk);
    char error[256];
    int ok = compile("proc main() -> int { create table t (id int); return 0; }", &chunk, error, sizeof(error));
    ASSERT_INT_EQ(1, ok);
    free_chunk(&chunk);
}

int main(void) {
    RUN_TEST(compiler_compiles_integer_return);
    RUN_TEST(compiler_compiles_local_variables);
    RUN_TEST(compiler_compiles_arithmetic);
    RUN_TEST(compiler_compiles_comparison);
    RUN_TEST(compiler_compiles_if_statement);
    RUN_TEST(compiler_compiles_if_true_branch);
    RUN_TEST(compiler_compiles_else_branch);
    RUN_TEST(compiler_compiles_else_if_chain);
    RUN_TEST(compiler_compiles_while_loop);
    RUN_TEST(compiler_compiles_while_break);
    RUN_TEST(compiler_compiles_while_continue);
    RUN_TEST(compiler_compiles_foreach_range);
    RUN_TEST(compiler_compiles_foreach_array);
    RUN_TEST(compiler_compiles_parse_int_native);
    RUN_TEST(compiler_compiles_split_lines_and_join_paths);
    RUN_TEST(compiler_compiles_unary_minus);
    RUN_TEST(compiler_compiles_unary_not);
    RUN_TEST(compiler_compiles_unary_not_true);
    RUN_TEST(compiler_compiles_float_return);
    RUN_TEST(compiler_compiles_float_arithmetic);
    RUN_TEST(compiler_compiles_mixed_int_float_arithmetic);
    RUN_TEST(compiler_compiles_string_return);
    RUN_TEST(compiler_compiles_procedure_call);
    RUN_TEST(compiler_compiles_forward_procedure_call);
    RUN_TEST(compiler_compiles_procedure_with_parameters);
    RUN_TEST(compiler_compiles_procedure_with_parameter_and_local);
    RUN_TEST(compiler_compiles_forward_call_with_arguments);
    RUN_TEST(compiler_compiles_for_sql_loop);
    RUN_TEST(compiler_compiles_for_sql_loop_sum);
    RUN_TEST(compiler_compiles_select_into_single_value);
    RUN_TEST(compiler_compiles_select_into_multi_value);
    RUN_TEST(compiler_compiles_select_into_array_row);
    RUN_TEST(compiler_rejects_select_into_no_row);
    RUN_TEST(compiler_block_scope_does_not_leak_locals);
    RUN_TEST(compiler_reports_undefined_variable);
    RUN_TEST(compiler_returns_error_message_for_undefined_variable);
    RUN_TEST(compiler_compiles_print_statement);
    RUN_TEST(compiler_compiles_print_string_statement);
    RUN_TEST(compiler_compiles_bool_literal);
    RUN_TEST(compiler_compiles_array_literal_and_index);
    RUN_TEST(compiler_compiles_index_assignment);
    RUN_TEST(compiler_compiles_array_length);
    RUN_TEST(compiler_compiles_array_append);
    RUN_TEST(compiler_compiles_println);
    RUN_TEST(compiler_compiles_clock);
    RUN_TEST(compiler_compiles_imported_procedure);
    RUN_TEST(compiler_reports_original_error_for_bad_import);
    RUN_TEST(compiler_rejects_too_many_imports);
    RUN_TEST(compiler_rejects_type_mismatch_in_assignment);
    RUN_TEST(compiler_accepts_typed_array_program);
    RUN_TEST(compiler_typechecks_sql_row_field_with_context);
    RUN_TEST(compiler_compiles_string_concatenation);
    RUN_TEST(compiler_compiles_string_comparison);
    RUN_TEST(compiler_compiles_string_length);
    RUN_TEST(compiler_compiles_concat_native);
    RUN_TEST(compiler_compiles_substring_native);
    RUN_TEST(compiler_compiles_int_to_string_native);
    RUN_TEST(compiler_compiles_abs_int_native);
    RUN_TEST(compiler_compiles_min_max_int_natives);
    RUN_TEST(compiler_compiles_abs_float_native);
    RUN_TEST(compiler_compiles_nested_index_assignment);
    RUN_TEST(compiler_emits_sql_exec);
    TEST_SUMMARY();
}
