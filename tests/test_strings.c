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

int main(void) {
    RUN_TEST(strings_value_add_concatenates);
    RUN_TEST(strings_value_lt_compares_lexicographically);
    RUN_TEST(strings_value_gt_compares_lexicographically);
    RUN_TEST(natives_length_accepts_strings);
    TEST_SUMMARY();
}
