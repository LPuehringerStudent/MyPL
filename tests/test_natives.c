#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "natives.h"
#include "vm.h"
#include "compiler.h"
#include "test_harness.h"

TEST(natives_finds_registered_functions) {
    ASSERT_INT_EQ(1, native_find("length") >= 0);
    ASSERT_INT_EQ(1, native_find("append") >= 0);
    ASSERT_INT_EQ(1, native_find("println") >= 0);
    ASSERT_INT_EQ(1, native_find("clock") >= 0);
    ASSERT_INT_EQ(0, native_find("not_a_native") >= 0);
}

TEST(natives_length_returns_array_length) {
    VM* vm = vm_init();
    ArrayObj* array = array_new();
    array_append(array, value_int(1));
    array_append(array, value_int(2));
    array_append(array, value_int(3));

    Value argv[1];
    argv[0] = value_array(array);
    Value result;
    ASSERT_INT_EQ(1, native_call(vm, native_find("length"), 1, argv, &result));
    ASSERT_INT_EQ(VAL_INT, result.type);
    ASSERT_INT_EQ(3, result.as.as_int);

    vm_free(vm);
}

TEST(natives_append_adds_element) {
    VM* vm = vm_init();
    ArrayObj* array = array_new();
    array_append(array, value_int(1));

    Value argv[2];
    argv[0] = value_array(array);
    argv[1] = value_int(2);
    Value result;
    ASSERT_INT_EQ(1, native_call(vm, native_find("append"), 2, argv, &result));
    ASSERT_INT_EQ(VAL_ARRAY, result.type);
    ASSERT_INT_EQ(2, array_length(result.as.as_array));

    vm_free(vm);
}

TEST(natives_clock_returns_non_negative_int) {
    VM* vm = vm_init();
    Value result;
    ASSERT_INT_EQ(1, native_call(vm, native_find("clock"), 0, NULL, &result));
    ASSERT_INT_EQ(VAL_INT, result.type);
    ASSERT_INT_EQ(1, result.as.as_int >= 0);
    vm_free(vm);
}

TEST(natives_wrong_arity_fails) {
    VM* vm = vm_init();
    Value result;
    Value argv[2];
    argv[0] = value_int(1);
    argv[1] = value_int(2);
    ASSERT_INT_EQ(0, native_call(vm, native_find("length"), 2, argv, &result));
    ASSERT_PTR_NOT_NULL(vm_get_error(vm));
    vm_free(vm);
}

TEST(natives_abs_int_returns_absolute_value) {
    VM* vm = vm_init();
    Value argv[1];
    argv[0] = value_int(-5);
    Value result;
    ASSERT_INT_EQ(1, native_call(vm, native_find("abs_int"), 1, argv, &result));
    ASSERT_INT_EQ(VAL_INT, result.type);
    ASSERT_INT_EQ(5, result.as.as_int);

    argv[0] = value_int(7);
    ASSERT_INT_EQ(1, native_call(vm, native_find("abs_int"), 1, argv, &result));
    ASSERT_INT_EQ(7, result.as.as_int);

    vm_free(vm);
}

TEST(natives_abs_int_rejects_float) {
    VM* vm = vm_init();
    Value argv[1];
    argv[0] = value_float(-3.5);
    Value result;
    ASSERT_INT_EQ(0, native_call(vm, native_find("abs_int"), 1, argv, &result));
    ASSERT_PTR_NOT_NULL(vm_get_error(vm));
    vm_free(vm);
}

TEST(natives_abs_float_returns_absolute_value) {
    VM* vm = vm_init();
    Value argv[1];
    argv[0] = value_float(-3.5);
    Value result;
    ASSERT_INT_EQ(1, native_call(vm, native_find("abs_float"), 1, argv, &result));
    ASSERT_INT_EQ(VAL_FLOAT, result.type);
    ASSERT_INT_EQ(1, result.as.as_float == 3.5);

    argv[0] = value_float(2.0);
    ASSERT_INT_EQ(1, native_call(vm, native_find("abs_float"), 1, argv, &result));
    ASSERT_INT_EQ(1, result.as.as_float == 2.0);

    vm_free(vm);
}

TEST(natives_abs_float_rejects_int) {
    VM* vm = vm_init();
    Value argv[1];
    argv[0] = value_int(-3);
    Value result;
    ASSERT_INT_EQ(0, native_call(vm, native_find("abs_float"), 1, argv, &result));
    ASSERT_PTR_NOT_NULL(vm_get_error(vm));
    vm_free(vm);
}

TEST(natives_abs_int_rejects_int_min) {
    VM* vm = vm_init();
    Value argv[1];
    argv[0] = value_int(INT_MIN);
    Value result;
    ASSERT_INT_EQ(0, native_call(vm, native_find("abs_int"), 1, argv, &result));
    ASSERT_PTR_NOT_NULL(vm_get_error(vm));
    vm_free(vm);
}

TEST(natives_min_int_returns_smaller) {
    VM* vm = vm_init();
    Value argv[2];
    argv[0] = value_int(3);
    argv[1] = value_int(7);
    Value result;
    ASSERT_INT_EQ(1, native_call(vm, native_find("min_int"), 2, argv, &result));
    ASSERT_INT_EQ(VAL_INT, result.type);
    ASSERT_INT_EQ(3, result.as.as_int);

    argv[0] = value_int(10);
    argv[1] = value_int(2);
    ASSERT_INT_EQ(1, native_call(vm, native_find("min_int"), 2, argv, &result));
    ASSERT_INT_EQ(2, result.as.as_int);

    vm_free(vm);
}

TEST(natives_max_int_returns_larger) {
    VM* vm = vm_init();
    Value argv[2];
    argv[0] = value_int(3);
    argv[1] = value_int(7);
    Value result;
    ASSERT_INT_EQ(1, native_call(vm, native_find("max_int"), 2, argv, &result));
    ASSERT_INT_EQ(VAL_INT, result.type);
    ASSERT_INT_EQ(7, result.as.as_int);

    argv[0] = value_int(10);
    argv[1] = value_int(2);
    ASSERT_INT_EQ(1, native_call(vm, native_find("max_int"), 2, argv, &result));
    ASSERT_INT_EQ(10, result.as.as_int);

    vm_free(vm);
}

TEST(natives_min_max_int_reject_float) {
    VM* vm = vm_init();
    Value argv[2];
    argv[0] = value_int(1);
    argv[1] = value_float(2.0);
    Value result;
    ASSERT_INT_EQ(0, native_call(vm, native_find("min_int"), 2, argv, &result));
    ASSERT_PTR_NOT_NULL(vm_get_error(vm));

    ASSERT_INT_EQ(0, native_call(vm, native_find("max_int"), 2, argv, &result));
    ASSERT_PTR_NOT_NULL(vm_get_error(vm));
    vm_free(vm);
}

TEST(natives_min_float_returns_smaller) {
    VM* vm = vm_init();
    Value argv[2];
    argv[0] = value_float(3.5);
    argv[1] = value_float(7.2);
    Value result;
    ASSERT_INT_EQ(1, native_call(vm, native_find("min_float"), 2, argv, &result));
    ASSERT_INT_EQ(VAL_FLOAT, result.type);
    ASSERT_INT_EQ(1, result.as.as_float == 3.5);
    vm_free(vm);
}

TEST(natives_max_float_returns_larger) {
    VM* vm = vm_init();
    Value argv[2];
    argv[0] = value_float(3.5);
    argv[1] = value_float(7.2);
    Value result;
    ASSERT_INT_EQ(1, native_call(vm, native_find("max_float"), 2, argv, &result));
    ASSERT_INT_EQ(VAL_FLOAT, result.type);
    ASSERT_INT_EQ(1, result.as.as_float == 7.2);
    vm_free(vm);
}

TEST(natives_min_max_float_reject_int) {
    VM* vm = vm_init();
    Value argv[2];
    argv[0] = value_float(1.0);
    argv[1] = value_int(2);
    Value result;
    ASSERT_INT_EQ(0, native_call(vm, native_find("min_float"), 2, argv, &result));
    ASSERT_PTR_NOT_NULL(vm_get_error(vm));

    ASSERT_INT_EQ(0, native_call(vm, native_find("max_float"), 2, argv, &result));
    ASSERT_PTR_NOT_NULL(vm_get_error(vm));
    vm_free(vm);
}

TEST(natives_range_returns_int_array) {
    VM* vm = vm_init();
    Value argv[2];
    argv[0] = value_int(1);
    argv[1] = value_int(4);
    Value result;
    ASSERT_INT_EQ(1, native_call(vm, native_find("range"), 2, argv, &result));
    ASSERT_INT_EQ(VAL_ARRAY, result.type);
    ASSERT_INT_EQ(3, array_length(result.as.as_array));
    ASSERT_INT_EQ(1, array_get(result.as.as_array, 0).as.as_int);
    ASSERT_INT_EQ(2, array_get(result.as.as_array, 1).as.as_int);
    ASSERT_INT_EQ(3, array_get(result.as.as_array, 2).as.as_int);
    vm_free(vm);
}

TEST(natives_assert_passes_on_true) {
    VM* vm = vm_init();
    Value argv[2];
    argv[0] = value_bool(1);
    argv[1] = value_string(strdup("should not fail"));
    Value result;
    ASSERT_INT_EQ(1, native_call(vm, native_find("assert"), 2, argv, &result));
    vm_free(vm);
}

TEST(natives_assert_fails_on_false) {
    VM* vm = vm_init();
    Value argv[2];
    argv[0] = value_bool(0);
    argv[1] = value_string(strdup("bad value"));
    Value result;
    ASSERT_INT_EQ(0, native_call(vm, native_find("assert"), 2, argv, &result));
    ASSERT_PTR_NOT_NULL(strstr(vm_get_error(vm), "bad value"));
    vm_free(vm);
}

TEST(natives_parse_int_parses_valid) {
    VM* vm = vm_init();
    Value argv[1];
    argv[0] = value_string(strdup("-42"));
    Value result;
    ASSERT_INT_EQ(1, native_call(vm, native_find("parse_int"), 1, argv, &result));
    ASSERT_INT_EQ(VAL_INT, result.type);
    ASSERT_INT_EQ(-42, result.as.as_int);
    vm_free(vm);
}

TEST(natives_parse_int_rejects_invalid) {
    VM* vm = vm_init();
    Value argv[1];
    argv[0] = value_string(strdup("abc"));
    Value result;
    ASSERT_INT_EQ(0, native_call(vm, native_find("parse_int"), 1, argv, &result));
    ASSERT_PTR_NOT_NULL(vm_get_error(vm));
    vm_free(vm);
}

TEST(natives_split_lines_splits_by_newline) {
    VM* vm = vm_init();
    Value argv[1];
    argv[0] = value_string(strdup("a\nb\nc"));
    Value result;
    ASSERT_INT_EQ(1, native_call(vm, native_find("split_lines"), 1, argv, &result));
    ASSERT_INT_EQ(VAL_ARRAY, result.type);
    ASSERT_INT_EQ(3, array_length(result.as.as_array));
    ASSERT_STRING_EQ("a", array_get(result.as.as_array, 0).as.as_string);
    ASSERT_STRING_EQ("b", array_get(result.as.as_array, 1).as.as_string);
    ASSERT_STRING_EQ("c", array_get(result.as.as_array, 2).as.as_string);
    vm_free(vm);
}

TEST(natives_split_lines_preserves_empty_lines) {
    VM* vm = vm_init();
    Value argv[1];
    argv[0] = value_string(strdup("a\n\nb"));
    Value result;
    ASSERT_INT_EQ(1, native_call(vm, native_find("split_lines"), 1, argv, &result));
    ASSERT_INT_EQ(VAL_ARRAY, result.type);
    ASSERT_INT_EQ(3, array_length(result.as.as_array));
    ASSERT_STRING_EQ("a", array_get(result.as.as_array, 0).as.as_string);
    ASSERT_STRING_EQ("", array_get(result.as.as_array, 1).as.as_string);
    ASSERT_STRING_EQ("b", array_get(result.as.as_array, 2).as.as_string);
    vm_free(vm);
}

TEST(natives_join_paths_joins_two_strings) {
    VM* vm = vm_init();
    Value argv[2];
    argv[0] = value_string(strdup("/home"));
    argv[1] = value_string(strdup("user"));
    Value result;
    ASSERT_INT_EQ(1, native_call(vm, native_find("join_paths"), 2, argv, &result));
    ASSERT_INT_EQ(VAL_STRING, result.type);
    ASSERT_STRING_EQ("/home/user", result.as.as_string);
    vm_free(vm);
}

TEST(natives_join_paths_avoids_double_slash) {
    VM* vm = vm_init();
    Value argv[2];
    argv[0] = value_string(strdup("/home/"));
    argv[1] = value_string(strdup("user"));
    Value result;
    ASSERT_INT_EQ(1, native_call(vm, native_find("join_paths"), 2, argv, &result));
    ASSERT_INT_EQ(VAL_STRING, result.type);
    ASSERT_STRING_EQ("/home/user", result.as.as_string);
    vm_free(vm);
}

TEST(natives_join_paths_respects_absolute_second) {
    VM* vm = vm_init();
    Value argv[2];
    argv[0] = value_string(strdup("/home"));
    argv[1] = value_string(strdup("/etc"));
    Value result;
    ASSERT_INT_EQ(1, native_call(vm, native_find("join_paths"), 2, argv, &result));
    ASSERT_INT_EQ(VAL_STRING, result.type);
    ASSERT_STRING_EQ("/etc", result.as.as_string);
    vm_free(vm);
}

int main(void) {
    RUN_TEST(natives_finds_registered_functions);
    RUN_TEST(natives_length_returns_array_length);
    RUN_TEST(natives_append_adds_element);
    RUN_TEST(natives_clock_returns_non_negative_int);
    RUN_TEST(natives_wrong_arity_fails);
    RUN_TEST(natives_abs_int_returns_absolute_value);
    RUN_TEST(natives_abs_int_rejects_float);
    RUN_TEST(natives_abs_float_returns_absolute_value);
    RUN_TEST(natives_abs_float_rejects_int);
    RUN_TEST(natives_abs_int_rejects_int_min);
    RUN_TEST(natives_min_int_returns_smaller);
    RUN_TEST(natives_max_int_returns_larger);
    RUN_TEST(natives_min_max_int_reject_float);
    RUN_TEST(natives_min_float_returns_smaller);
    RUN_TEST(natives_max_float_returns_larger);
    RUN_TEST(natives_min_max_float_reject_int);
    RUN_TEST(natives_range_returns_int_array);
    RUN_TEST(natives_assert_passes_on_true);
    RUN_TEST(natives_assert_fails_on_false);
    RUN_TEST(natives_parse_int_parses_valid);
    RUN_TEST(natives_parse_int_rejects_invalid);
    RUN_TEST(natives_split_lines_splits_by_newline);
    RUN_TEST(natives_split_lines_preserves_empty_lines);
    RUN_TEST(natives_join_paths_joins_two_strings);
    RUN_TEST(natives_join_paths_avoids_double_slash);
    RUN_TEST(natives_join_paths_respects_absolute_second);
    TEST_SUMMARY();
}
