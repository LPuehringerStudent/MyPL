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

int main(void) {
    RUN_TEST(strings_value_add_concatenates);
    TEST_SUMMARY();
}
