#include "test_harness.h"
#include "compiler.h"
#include "natives.h"
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

/* array_append retains what it stores, so an array a native builds must hold
   the only reference to each new element, and one extra reference to each
   element it copies from another array (#82). */
static int call_array_native(VM* vm, const char* name, int argc, Value* argv, Value* out) {
    *out = value_int(0);
    return native_call(vm, native_find(name), argc, argv, out) && out->type == VAL_ARRAY;
}

TEST(gc_split_natives_give_each_element_one_owner) {
    VM* vm = vm_init();
    Value argv[2] = {value_string(strdup("a,b,c")), value_string(strdup(","))};
    Value parts;
    ASSERT_INT_EQ(1, call_array_native(vm, "split", 2, argv, &parts));
    ASSERT_INT_EQ(3, array_length(parts.as.as_array));
    for (int i = 0; i < 3; i++) {
        ASSERT_INT_EQ(1, value_ref_count(array_get(parts.as.as_array, i)));
    }
    value_release(parts);

    Value text = value_string(strdup("x\ny"));
    Value lines;
    ASSERT_INT_EQ(1, call_array_native(vm, "split_lines", 1, &text, &lines));
    ASSERT_INT_EQ(2, array_length(lines.as.as_array));
    for (int i = 0; i < 2; i++) {
        ASSERT_INT_EQ(1, value_ref_count(array_get(lines.as.as_array, i)));
    }
    value_release(lines);

    value_release(text);
    value_release(argv[0]);
    value_release(argv[1]);
    vm_free(vm);
}

TEST(gc_copying_array_natives_take_one_reference_per_element) {
    VM* vm = vm_init();
    Value s = value_string(strdup("shared"));
    Value source = value_array(array_new());
    array_append(source.as.as_array, s);
    array_append(source.as.as_array, value_int(7));
    ASSERT_INT_EQ(2, value_ref_count(s));

    Value slice_args[3] = {source, value_int(0), value_int(2)};
    Value sliced;
    ASSERT_INT_EQ(1, call_array_native(vm, "slice", 3, slice_args, &sliced));
    ASSERT_INT_EQ(3, value_ref_count(s));
    value_release(sliced);
    ASSERT_INT_EQ(2, value_ref_count(s));

    Value remove_args[2] = {source, value_int(1)};
    Value removed;
    ASSERT_INT_EQ(1, call_array_native(vm, "remove_at", 2, remove_args, &removed));
    ASSERT_INT_EQ(3, value_ref_count(s));
    value_release(removed);
    ASSERT_INT_EQ(2, value_ref_count(s));

    Value insert_args[3] = {source, value_int(0), s};
    Value inserted;
    ASSERT_INT_EQ(1, call_array_native(vm, "insert", 3, insert_args, &inserted));
    ASSERT_INT_EQ(4, value_ref_count(s)); /* the inserted copy and the moved one */
    value_release(inserted);
    ASSERT_INT_EQ(2, value_ref_count(s));

    Value fill_args[2] = {value_int(3), s};
    Value filled;
    ASSERT_INT_EQ(1, call_array_native(vm, "array_fill", 2, fill_args, &filled));
    ASSERT_INT_EQ(5, value_ref_count(s));
    value_release(filled);
    ASSERT_INT_EQ(2, value_ref_count(s));

    value_release(source);
    ASSERT_INT_EQ(1, value_ref_count(s));
    value_release(s);
    vm_free(vm);
}

TEST(gc_dbms_output_buffer_takes_one_reference_per_line) {
    VM* vm = vm_init();
    Value line = value_string(strdup("hello"));
    vm_dbms_output_enable(vm, 10);
    vm_dbms_output_put_line(vm, line);
    ASSERT_INT_EQ(2, value_ref_count(line));

    Value lines = vm_dbms_output_get_lines(vm);
    ASSERT_INT_EQ(1, array_length(lines.as.as_array));
    ASSERT_INT_EQ(2, value_ref_count(line)); /* moved from the buffer to the result */
    value_release(lines);
    ASSERT_INT_EQ(1, value_ref_count(line));

    vm_dbms_output_put_line(vm, line);
    vm_dbms_output_disable(vm);
    ASSERT_INT_EQ(1, value_ref_count(line));
    value_release(line);
    vm_free(vm);
}

int main(void) {
    RUN_TEST(gc_string_starts_with_ref_count_one);
    RUN_TEST(gc_array_starts_with_ref_count_one);
    RUN_TEST(gc_retain_increments_ref_count);
    RUN_TEST(gc_array_releases_elements_when_freed);
    RUN_TEST(gc_overwriting_array_local_runs_cleanly);
    RUN_TEST(gc_string_concatenation_does_not_leak);
    RUN_TEST(gc_nested_arrays_are_freed);
    RUN_TEST(gc_split_natives_give_each_element_one_owner);
    RUN_TEST(gc_copying_array_natives_take_one_reference_per_element);
    RUN_TEST(gc_dbms_output_buffer_takes_one_reference_per_line);
    TEST_SUMMARY();
}
