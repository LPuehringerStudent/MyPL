#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_harness.h"
#include "compiler.h"
#include "natives.h"
#include "vm.h"

static int run_native_string(const char* name, int argc, Value* argv, Value* out) {
    VM* vm = vm_init();
    int ok = native_call(vm, native_find(name), argc, argv, out);
    if (!ok) {
        fprintf(stderr, "native error: %s\n", vm_get_error(vm));
    }
    vm_free(vm);
    return ok;
}

TEST(strings_value_add_concatenates) {
    Value a = value_string(strdup("hello"));
    Value b = value_string(strdup(" world"));
    Value result = value_add(a, b);
    ASSERT_INT_EQ(VAL_STRING, result.type);
    ASSERT_STRING_EQ("hello world", result.as.as_string);
}

TEST(strings_value_lt_compares_lexicographically) {
    Value a = value_string(strdup("apple"));
    Value b = value_string(strdup("banana"));
    ASSERT_INT_EQ(1, value_lt(a, b).as.as_int);
    ASSERT_INT_EQ(0, value_lt(b, a).as.as_int);
}

TEST(strings_value_gt_compares_lexicographically) {
    Value a = value_string(strdup("apple"));
    Value b = value_string(strdup("banana"));
    ASSERT_INT_EQ(0, value_gt(a, b).as.as_int);
    ASSERT_INT_EQ(1, value_gt(b, a).as.as_int);
}

TEST(natives_length_accepts_strings) {
    Value argv[1];
    argv[0] = value_string(strdup("hello"));
    Value out;
    ASSERT_INT_EQ(1, run_native_string("length", 1, argv, &out));
    ASSERT_INT_EQ(VAL_INT, out.type);
    ASSERT_INT_EQ(5, out.as.as_int);
}

TEST(natives_concat_returns_concatenated_string) {
    Value argv[2];
    argv[0] = value_string(strdup("Hello, "));
    argv[1] = value_string(strdup("world!"));
    Value out;
    ASSERT_INT_EQ(1, run_native_string("concat", 2, argv, &out));
    ASSERT_INT_EQ(VAL_STRING, out.type);
    ASSERT_STRING_EQ("Hello, world!", out.as.as_string);
}

TEST(natives_concat_rejects_non_strings) {
    Value argv[2];
    argv[0] = value_string(strdup("x"));
    argv[1] = value_int(1);
    Value out;
    ASSERT_INT_EQ(0, run_native_string("concat", 2, argv, &out));
}

TEST(natives_substring_extracts_substring) {
    Value argv[3];
    argv[0] = value_string(strdup("hello"));
    argv[1] = value_int(1);
    argv[2] = value_int(3);
    Value out;
    ASSERT_INT_EQ(1, run_native_string("substring", 3, argv, &out));
    ASSERT_STRING_EQ("ell", out.as.as_string);
}

TEST(natives_substring_clamps_out_of_range) {
    Value argv[3];
    argv[0] = value_string(strdup("hi"));
    argv[1] = value_int(1);
    argv[2] = value_int(100);
    Value out;
    ASSERT_INT_EQ(1, run_native_string("substring", 3, argv, &out));
    ASSERT_STRING_EQ("i", out.as.as_string);
}

TEST(natives_contains_finds_substring) {
    Value argv[2];
    argv[0] = value_string(strdup("hello world"));
    argv[1] = value_string(strdup("world"));
    Value out;
    ASSERT_INT_EQ(1, run_native_string("contains", 2, argv, &out));
    ASSERT_INT_EQ(1, out.as.as_int);
}

TEST(natives_contains_returns_false_when_missing) {
    Value argv[2];
    argv[0] = value_string(strdup("hello"));
    argv[1] = value_string(strdup("x"));
    Value out;
    ASSERT_INT_EQ(1, run_native_string("contains", 2, argv, &out));
    ASSERT_INT_EQ(0, out.as.as_int);
}

TEST(natives_index_of_returns_first_index) {
    Value argv[2];
    argv[0] = value_string(strdup("banana"));
    argv[1] = value_string(strdup("na"));
    Value out;
    ASSERT_INT_EQ(1, run_native_string("index_of", 2, argv, &out));
    ASSERT_INT_EQ(2, out.as.as_int);
}

TEST(natives_index_of_returns_minus_one_when_missing) {
    Value argv[2];
    argv[0] = value_string(strdup("abc"));
    argv[1] = value_string(strdup("z"));
    Value out;
    ASSERT_INT_EQ(1, run_native_string("index_of", 2, argv, &out));
    ASSERT_INT_EQ(-1, out.as.as_int);
}

int main(void) {
    RUN_TEST(strings_value_add_concatenates);
    RUN_TEST(strings_value_lt_compares_lexicographically);
    RUN_TEST(strings_value_gt_compares_lexicographically);
    RUN_TEST(natives_length_accepts_strings);
    RUN_TEST(natives_concat_returns_concatenated_string);
    RUN_TEST(natives_concat_rejects_non_strings);
    RUN_TEST(natives_substring_extracts_substring);
    RUN_TEST(natives_substring_clamps_out_of_range);
    RUN_TEST(natives_contains_finds_substring);
    RUN_TEST(natives_contains_returns_false_when_missing);
    RUN_TEST(natives_index_of_returns_first_index);
    RUN_TEST(natives_index_of_returns_minus_one_when_missing);
    TEST_SUMMARY();
}
