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

Value value_add(Value a, Value b) {
    return value_int(a.as.as_int + b.as.as_int);
}

Value value_sub(Value a, Value b) {
    return value_int(a.as.as_int - b.as.as_int);
}

Value value_mul(Value a, Value b) {
    return value_int(a.as.as_int * b.as.as_int);
}

Value value_div(Value a, Value b) {
    return value_int(a.as.as_int / b.as.as_int);
}

Value value_eq(Value a, Value b) {
    return value_int(a.as.as_int == b.as.as_int ? 1 : 0);
}

Value value_lt(Value a, Value b) {
    return value_int(a.as.as_int < b.as.as_int ? 1 : 0);
}

Value value_gt(Value a, Value b) {
    return value_int(a.as.as_int > b.as.as_int ? 1 : 0);
}

int value_is_truthy(Value value) {
    return value.as.as_int != 0;
}
