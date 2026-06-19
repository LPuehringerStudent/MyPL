#include <stddef.h>
#include <string.h>

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

/* Helper: returns true when either operand is a float. */
static int either_float(Value a, Value b) {
    return a.type == VAL_FLOAT || b.type == VAL_FLOAT;
}

/* Helper: returns the numeric value as double. */
static double as_number(Value v) {
    if (v.type == VAL_FLOAT) return v.as.as_float;
    if (v.type == VAL_INT) return (double)v.as.as_int;
    return 0.0;
}

Value value_add(Value a, Value b) {
    if (either_float(a, b)) {
        return value_float(as_number(a) + as_number(b));
    }
    if (a.type == VAL_INT && b.type == VAL_INT) {
        return value_int(a.as.as_int + b.as.as_int);
    }
    return value_int(0);
}

Value value_sub(Value a, Value b) {
    if (either_float(a, b)) {
        return value_float(as_number(a) - as_number(b));
    }
    if (a.type == VAL_INT && b.type == VAL_INT) {
        return value_int(a.as.as_int - b.as.as_int);
    }
    return value_int(0);
}

Value value_mul(Value a, Value b) {
    if (either_float(a, b)) {
        return value_float(as_number(a) * as_number(b));
    }
    if (a.type == VAL_INT && b.type == VAL_INT) {
        return value_int(a.as.as_int * b.as.as_int);
    }
    return value_int(0);
}

Value value_div(Value a, Value b) {
    if (either_float(a, b)) {
        double divisor = as_number(b);
        if (divisor == 0.0) return value_int(0);
        return value_float(as_number(a) / divisor);
    }
    if (a.type == VAL_INT && b.type == VAL_INT) {
        if (b.as.as_int == 0) return value_int(0);
        return value_int(a.as.as_int / b.as.as_int);
    }
    return value_int(0);
}

Value value_eq(Value a, Value b) {
    if (either_float(a, b)) {
        return value_int(as_number(a) == as_number(b) ? 1 : 0);
    }
    if (a.type == VAL_INT && b.type == VAL_INT) {
        return value_int(a.as.as_int == b.as.as_int ? 1 : 0);
    }
    if (a.type == VAL_STRING && b.type == VAL_STRING) {
        const char* as = a.as.as_string ? a.as.as_string : "";
        const char* bs = b.as.as_string ? b.as.as_string : "";
        return value_int(strcmp(as, bs) == 0 ? 1 : 0);
    }
    return value_int(0);
}

Value value_lt(Value a, Value b) {
    if (either_float(a, b)) {
        return value_int(as_number(a) < as_number(b) ? 1 : 0);
    }
    if (a.type == VAL_INT && b.type == VAL_INT) {
        return value_int(a.as.as_int < b.as.as_int ? 1 : 0);
    }
    return value_int(0);
}

Value value_gt(Value a, Value b) {
    if (either_float(a, b)) {
        return value_int(as_number(a) > as_number(b) ? 1 : 0);
    }
    if (a.type == VAL_INT && b.type == VAL_INT) {
        return value_int(a.as.as_int > b.as.as_int ? 1 : 0);
    }
    return value_int(0);
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
