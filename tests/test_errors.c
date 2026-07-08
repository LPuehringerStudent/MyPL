#include <string.h>

#include "test_harness.h"
#include "compiler.h"
#include "vm.h"
#include "parser.h"
#include "typecheck.h"
#include "ast.h"

TEST(parser_error_includes_location) {
    char error[256];
    Program* program = parse_with_path(
        "proc main() -> int { return 1; ", "parser.mypl", error, sizeof(error));
    ASSERT_PTR_NULL(program);
    ASSERT_PTR_NOT_NULL(strstr(error, "parser.mypl:1:"));
    ASSERT_PTR_NOT_NULL(strstr(error, ": error:"));
}

TEST(type_error_includes_location) {
    Chunk chunk;
    init_chunk(&chunk);
    char error[256];
    int ok = compile_with_path(
        "proc main() -> int {\n"
        "    int x = \"hello\";\n"
        "    return 0;\n"
        "}",
        &chunk, "typecheck.mypl", error, sizeof(error));
    ASSERT_INT_EQ(0, ok);
    ASSERT_PTR_NOT_NULL(strstr(error, "typecheck.mypl:2:"));
    ASSERT_PTR_NOT_NULL(strstr(error, ": error:"));
    ASSERT_PTR_NOT_NULL(strstr(error, "string"));
    free_chunk(&chunk);
}

TEST(compiler_error_includes_location) {
    Chunk chunk;
    init_chunk(&chunk);
    char error[256];
    int ok = compile_with_path(
        "proc main() -> int {\n"
        "    return undefined_var;\n"
        "}",
        &chunk, "compiler.mypl", error, sizeof(error));
    ASSERT_INT_EQ(0, ok);
    ASSERT_PTR_NOT_NULL(strstr(error, "compiler.mypl:2:"));
    ASSERT_PTR_NOT_NULL(strstr(error, ": error:"));
    ASSERT_PTR_NOT_NULL(strstr(error, "Undefined variable"));
    free_chunk(&chunk);
}

TEST(runtime_error_includes_location) {
    Chunk chunk;
    init_chunk(&chunk);
    char error[256];
    int ok = compile_with_path(
        "proc main() -> int {\n"
        "    int x = 0;\n"
        "    assert(x == 1, \"failed\");\n"
        "    return 0;\n"
        "}",
        &chunk, "runtime.mypl", error, sizeof(error));
    ASSERT_INT_EQ(1, ok);

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_RUNTIME_ERROR, vm_interpret(vm, &chunk));
    const char* msg = vm_get_error(vm);
    ASSERT_PTR_NOT_NULL(msg);
    ASSERT_PTR_NOT_NULL(strstr(msg, "runtime.mypl:3:"));
    ASSERT_PTR_NOT_NULL(strstr(msg, ": error:"));
    ASSERT_PTR_NOT_NULL(strstr(msg, "failed"));
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(native_error_includes_location) {
    Chunk chunk;
    init_chunk(&chunk);
    char error[256];
    int ok = compile_with_path(
        "proc main() -> int {\n"
        "    int x = parse_int(\"not a number\");\n"
        "    return x;\n"
        "}",
        &chunk, "native.mypl", error, sizeof(error));
    ASSERT_INT_EQ(1, ok);

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_RUNTIME_ERROR, vm_interpret(vm, &chunk));
    const char* msg = vm_get_error(vm);
    ASSERT_PTR_NOT_NULL(msg);
    ASSERT_PTR_NOT_NULL(strstr(msg, "native.mypl:2:"));
    ASSERT_PTR_NOT_NULL(strstr(msg, ": error:"));
    ASSERT_PTR_NOT_NULL(strstr(msg, "parse_int: invalid integer"));
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(error_format_without_path) {
    char error[256];
    Program* program = parse("proc main() -> int { return 1; ", error, sizeof(error));
    ASSERT_PTR_NULL(program);
    ASSERT_PTR_NOT_NULL(strstr(error, "1:"));
    ASSERT_PTR_NOT_NULL(strstr(error, ": error:"));
}

int main(void) {
    RUN_TEST(parser_error_includes_location);
    RUN_TEST(type_error_includes_location);
    RUN_TEST(compiler_error_includes_location);
    RUN_TEST(runtime_error_includes_location);
    RUN_TEST(native_error_includes_location);
    RUN_TEST(error_format_without_path);
    return 0;
}
