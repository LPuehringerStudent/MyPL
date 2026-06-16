#include "test_harness.h"
#include "compiler.h"

TEST(add_ints) {
    Value a = value_int(3);
    Value b = value_int(4);
    Value r = value_add(a, b);
    ASSERT_INT_EQ(7, r.as.as_int);
}

TEST(sub_ints) {
    Value a = value_int(10);
    Value b = value_int(3);
    Value r = value_sub(a, b);
    ASSERT_INT_EQ(7, r.as.as_int);
}

TEST(mul_ints) {
    Value a = value_int(6);
    Value b = value_int(7);
    Value r = value_mul(a, b);
    ASSERT_INT_EQ(42, r.as.as_int);
}

TEST(div_ints) {
    Value a = value_int(8);
    Value b = value_int(2);
    Value r = value_div(a, b);
    ASSERT_INT_EQ(4, r.as.as_int);
}

TEST(eq_ints) {
    Value a = value_int(5);
    Value b = value_int(5);
    Value r = value_eq(a, b);
    ASSERT_INT_EQ(1, r.as.as_int);
}

TEST(lt_ints) {
    Value a = value_int(3);
    Value b = value_int(5);
    Value r = value_lt(a, b);
    ASSERT_INT_EQ(1, r.as.as_int);
}

TEST(gt_ints) {
    Value a = value_int(7);
    Value b = value_int(2);
    Value r = value_gt(a, b);
    ASSERT_INT_EQ(1, r.as.as_int);
}

int main(void) {
    RUN_TEST(add_ints);
    RUN_TEST(sub_ints);
    RUN_TEST(mul_ints);
    RUN_TEST(div_ints);
    RUN_TEST(eq_ints);
    RUN_TEST(lt_ints);
    RUN_TEST(gt_ints);
    TEST_SUMMARY();
}
