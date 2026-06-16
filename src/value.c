#include <stddef.h>

#include "compiler.h"

Value value_int(int v) {
    Value value;
    value.type = VAL_INT;
    value.as.as_int = v;
    return value;
}

Value value_float(double v) {
    Value value;
    value.type = VAL_FLOAT;
    value.as.as_float = v;
    return value;
}

Value value_string(char* s) {
    Value value;
    value.type = VAL_STRING;
    value.as.as_string = s;
    return value;
}

/* Helper: returns true only when both operands are VAL_INT. */
static int both_ints(Value a, Value b) {
    return a.type == VAL_INT && b.type == VAL_INT;
}

Value value_add(Value a, Value b) {
    /* TODO: propagate runtime error for mismatched/unsupported operand types. */
    if (!both_ints(a, b)) return value_int(0);
    return value_int(a.as.as_int + b.as.as_int);
}

Value value_sub(Value a, Value b) {
    /* TODO: propagate runtime error for mismatched/unsupported operand types. */
    if (!both_ints(a, b)) return value_int(0);
    return value_int(a.as.as_int - b.as.as_int);
}

Value value_mul(Value a, Value b) {
    /* TODO: propagate runtime error for mismatched/unsupported operand types. */
    if (!both_ints(a, b)) return value_int(0);
    return value_int(a.as.as_int * b.as.as_int);
}

Value value_div(Value a, Value b) {
    /* TODO: propagate runtime error for mismatched/unsupported operand types. */
    if (!both_ints(a, b)) return value_int(0);
    return value_int(a.as.as_int / b.as.as_int);
}

Value value_eq(Value a, Value b) {
    /* TODO: propagate runtime error for mismatched/unsupported operand types. */
    if (!both_ints(a, b)) return value_int(0);
    return value_int(a.as.as_int == b.as.as_int ? 1 : 0);
}

Value value_lt(Value a, Value b) {
    /* TODO: propagate runtime error for mismatched/unsupported operand types. */
    if (!both_ints(a, b)) return value_int(0);
    return value_int(a.as.as_int < b.as.as_int ? 1 : 0);
}

Value value_gt(Value a, Value b) {
    /* TODO: propagate runtime error for mismatched/unsupported operand types. */
    if (!both_ints(a, b)) return value_int(0);
    return value_int(a.as.as_int > b.as.as_int ? 1 : 0);
}

int value_is_truthy(Value value) {
    switch (value.type) {
        case VAL_INT:
            return value.as.as_int != 0;
        case VAL_FLOAT:
            return value.as.as_float != 0.0;
        case VAL_STRING:
            return value.as.as_string != NULL;
        default:
            /* VAL_ROW_HANDLE and any future types: treat non-NULL as truthy. */
            return value.as.as_row_handle != NULL;
    }
}
