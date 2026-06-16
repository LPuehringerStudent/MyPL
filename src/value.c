#include "compiler.h"

Value value_int(int v) {
    Value value;
    value.type = 0;
    value.as.as_int = v;
    return value;
}

Value value_float(double v) {
    Value value;
    value.type = 1;
    value.as.as_float = v;
    return value;
}

Value value_string(char* s) {
    Value value;
    value.type = 2;
    value.as.as_string = s;
    return value;
}
