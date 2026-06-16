#include "test_harness.h"
#include "vm.h"

TEST(vm_can_push_and_pop_values) {
    VM* vm = vm_init();
    ASSERT_PTR_NOT_NULL(vm);
    vm_free(vm);
}

int main(void) {
    RUN_TEST(vm_can_push_and_pop_values);
    TEST_SUMMARY();
}
