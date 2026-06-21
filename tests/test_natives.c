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

int main(void) {
    RUN_TEST(natives_finds_registered_functions);
    RUN_TEST(natives_length_returns_array_length);
    RUN_TEST(natives_append_adds_element);
    RUN_TEST(natives_clock_returns_non_negative_int);
    RUN_TEST(natives_wrong_arity_fails);
    return 0;
}
