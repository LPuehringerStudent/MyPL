#include "test_harness.h"
#include "compiler.h"
#include "vm.h"

TEST(gc_string_starts_with_ref_count_one) {
    Value s = value_string(strdup("hello"));
    ASSERT_INT_EQ(VAL_STRING, s.type);
    ASSERT_INT_EQ(1, value_ref_count(s));
    value_release(s);
}

TEST(gc_array_starts_with_ref_count_one) {
    ArrayObj* a = array_new();
    Value v = value_array(a);
    ASSERT_INT_EQ(VAL_ARRAY, v.type);
    ASSERT_INT_EQ(1, value_ref_count(v));
    value_release(v);
}

TEST(gc_retain_increments_ref_count) {
    Value s = value_string(strdup("x"));
    value_retain(s);
    ASSERT_INT_EQ(2, value_ref_count(s));
    value_release(s);
    ASSERT_INT_EQ(1, value_ref_count(s));
    value_release(s);
}

TEST(gc_array_releases_elements_when_freed) {
    ArrayObj* inner = array_new();
    Value inner_v = value_array(inner);
    ASSERT_INT_EQ(1, value_ref_count(inner_v));

    ArrayObj* outer = array_new();
    Value outer_v = value_array(outer);
    ASSERT_INT_EQ(1, value_ref_count(outer_v));

    array_append(outer, inner_v);
    ASSERT_INT_EQ(2, value_ref_count(inner_v));

    value_release(inner_v);
    ASSERT_INT_EQ(1, value_ref_count(inner_v));

    value_release(outer_v);
}

TEST(gc_overwriting_array_local_runs_cleanly) {
    Chunk chunk;
    init_chunk(&chunk);
    char error[256];
    const char* source =
        "proc main() -> int { "
        "  array<int> a = [1, 2, 3]; "
        "  a = [4, 5, 6]; "
        "  return length(a); "
        "}";
    ASSERT_INT_EQ(1, compile(source, &chunk, error, sizeof(error)));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    ASSERT_INT_EQ(3, vm_pop(vm).as.as_int);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(gc_string_concatenation_does_not_leak) {
    Chunk chunk;
    init_chunk(&chunk);
    char error[256];
    const char* source =
        "proc main() -> string { "
        "  string a = \"hello\"; "
        "  string b = \" world\"; "
        "  return a + b; "
        "}";
    ASSERT_INT_EQ(1, compile(source, &chunk, error, sizeof(error)));

    VM* vm = vm_init();
    ASSERT_INT_EQ(INTERPRET_OK, vm_interpret(vm, &chunk));
    Value result = vm_pop(vm);
    ASSERT_STRING_EQ("hello world", result.as.as_string);
    vm_free(vm);
    free_chunk(&chunk);
}

TEST(gc_nested_arrays_are_freed) {
    ArrayObj* inner = array_new();
    array_append(inner, value_int(42));
    Value inner_v = value_array(inner);

    ArrayObj* outer = array_new();
    Value outer_v = value_array(outer);
    array_append(outer, inner_v);

    value_release(outer_v);
}

int main(void) {
    RUN_TEST(gc_string_starts_with_ref_count_one);
    RUN_TEST(gc_array_starts_with_ref_count_one);
    RUN_TEST(gc_retain_increments_ref_count);
    RUN_TEST(gc_array_releases_elements_when_freed);
    RUN_TEST(gc_overwriting_array_local_runs_cleanly);
    RUN_TEST(gc_string_concatenation_does_not_leak);
    RUN_TEST(gc_nested_arrays_are_freed);
    TEST_SUMMARY();
}
