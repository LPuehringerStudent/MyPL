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

TEST(value_types_use_enum_constants) {
    Value i = value_int(1);
    Value f = value_float(1.5);
    Value s = value_string(strdup("x"));
    ASSERT_INT_EQ(VAL_INT, i.type);
    ASSERT_INT_EQ(VAL_FLOAT, f.type);
    ASSERT_INT_EQ(VAL_STRING, s.type);
    value_release(s);
}

TEST(add_int_and_float_returns_float) {
    Value a = value_int(3);
    Value b = value_float(4.0);
    Value r = value_add(a, b);
    ASSERT_INT_EQ(VAL_FLOAT, r.type);
    ASSERT_FLOAT_EQ(7.0, r.as.as_float);
}

TEST(sub_int_and_string_returns_zero) {
    Value a = value_int(3);
    Value b = value_string(strdup("x"));
    Value r = value_sub(a, b);
    ASSERT_INT_EQ(0, r.as.as_int);
    value_release(b);
}

TEST(mul_float_and_int_returns_float) {
    Value a = value_float(3.0);
    Value b = value_int(4);
    Value r = value_mul(a, b);
    ASSERT_INT_EQ(VAL_FLOAT, r.type);
    ASSERT_FLOAT_EQ(12.0, r.as.as_float);
}

TEST(div_string_and_int_returns_zero) {
    Value a = value_string(strdup("x"));
    Value b = value_int(4);
    Value r = value_div(a, b);
    ASSERT_INT_EQ(0, r.as.as_int);
    value_release(a);
}

TEST(eq_int_and_float_coerces) {
    Value a = value_int(3);
    Value b = value_float(3.0);
    Value r = value_eq(a, b);
    ASSERT_INT_EQ(1, r.as.as_int);
}

TEST(lt_int_and_float_coerces) {
    Value a = value_int(3);
    Value b = value_float(4.0);
    Value r = value_lt(a, b);
    ASSERT_INT_EQ(1, r.as.as_int);
}

TEST(gt_float_and_int_coerces) {
    Value a = value_float(7.0);
    Value b = value_int(2);
    Value r = value_gt(a, b);
    ASSERT_INT_EQ(1, r.as.as_int);
}

TEST(is_truthy_int_zero_is_false) {
    ASSERT_INT_EQ(0, value_is_truthy(value_int(0)));
}

TEST(is_truthy_int_nonzero_is_true) {
    ASSERT_INT_EQ(1, value_is_truthy(value_int(42)));
}

TEST(is_truthy_float_zero_is_false) {
    ASSERT_INT_EQ(0, value_is_truthy(value_float(0.0)));
}

TEST(is_truthy_float_nonzero_is_true) {
    ASSERT_INT_EQ(1, value_is_truthy(value_float(1.5)));
}

TEST(is_truthy_null_string_is_false) {
    ASSERT_INT_EQ(0, value_is_truthy(value_string(NULL)));
}

TEST(is_truthy_non_null_string_is_true) {
    Value v = value_string(strdup("hello"));
    ASSERT_INT_EQ(1, value_is_truthy(v));
    value_release(v);
}

TEST(row_value_holds_handle_and_type) {
    int dummy_row = 42;
    Value v = value_row(&dummy_row);
    ASSERT_INT_EQ(VAL_ROW, v.type);
    ASSERT_PTR_EQ(&dummy_row, v.as.as_row_handle);
    value_release(v);
}

int main(void) {
    RUN_TEST(add_ints);
    RUN_TEST(sub_ints);
    RUN_TEST(mul_ints);
    RUN_TEST(div_ints);
    RUN_TEST(eq_ints);
    RUN_TEST(lt_ints);
    RUN_TEST(gt_ints);
    RUN_TEST(value_types_use_enum_constants);
    RUN_TEST(add_int_and_float_returns_float);
    RUN_TEST(sub_int_and_string_returns_zero);
    RUN_TEST(mul_float_and_int_returns_float);
    RUN_TEST(div_string_and_int_returns_zero);
    RUN_TEST(eq_int_and_float_coerces);
    RUN_TEST(lt_int_and_float_coerces);
    RUN_TEST(gt_float_and_int_coerces);
    RUN_TEST(is_truthy_int_zero_is_false);
    RUN_TEST(is_truthy_int_nonzero_is_true);
    RUN_TEST(is_truthy_float_zero_is_false);
    RUN_TEST(is_truthy_float_nonzero_is_true);
    RUN_TEST(is_truthy_null_string_is_false);
    RUN_TEST(is_truthy_non_null_string_is_true);
    RUN_TEST(row_value_holds_handle_and_type);
    TEST_SUMMARY();
}
