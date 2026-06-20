#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "compiler.h"

struct ArrayObj {
    Value* items;
    int count;
    int capacity;
    ArrayObj* next;
};

static ArrayObj* array_pool = NULL;

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

Value value_bool(int v) {
    Value value;
    value.type = VAL_BOOL;
    value.as.as_int = v ? 1 : 0;
    return value;
}

Value value_array(ArrayObj* array) {
    Value value;
    value.type = VAL_ARRAY;
    value.as.as_array = array;
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
        case VAL_BOOL:
            return value.as.as_int != 0;
        case VAL_ARRAY:
            return value.as.as_array != NULL;
        default:
            /* VAL_ROW_HANDLE and any future types: treat non-NULL as truthy. */
            return value.as.as_row_handle != NULL;
    }
}

void value_print(Value value) {
    switch (value.type) {
        case VAL_INT:
            printf("%d", value.as.as_int);
            break;
        case VAL_FLOAT:
            printf("%g", value.as.as_float);
            break;
        case VAL_STRING:
            printf("%s", value.as.as_string ? value.as.as_string : "");
            break;
        case VAL_BOOL:
            printf("%s", value.as.as_int ? "true" : "false");
            break;
        case VAL_ARRAY: {
            ArrayObj* array = value.as.as_array;
            printf("[");
            if (array != NULL) {
                for (int i = 0; i < array->count; i++) {
                    value_print(array->items[i]);
                    if (i < array->count - 1) printf(", ");
                }
            }
            printf("]");
            break;
        }
        default:
            printf("?");
            break;
    }
}

ArrayObj* array_new(void) {
    ArrayObj* array = malloc(sizeof(ArrayObj));
    if (array == NULL) return NULL;
    array->items = NULL;
    array->count = 0;
    array->capacity = 0;
    array->next = array_pool;
    array_pool = array;
    return array;
}

void array_free(ArrayObj* array) {
    if (array == NULL) return;
    free(array->items);
    free(array);
}

void array_pool_free_all(void) {
    while (array_pool != NULL) {
        ArrayObj* next = array_pool->next;
        array_free(array_pool);
        array_pool = next;
    }
}

void array_append(ArrayObj* array, Value value) {
    if (array->count >= array->capacity) {
        int new_capacity = array->capacity == 0 ? 4 : array->capacity * 2;
        Value* new_items = realloc(array->items, sizeof(Value) * (size_t)new_capacity);
        if (new_items == NULL) return;
        array->items = new_items;
        array->capacity = new_capacity;
    }
    array->items[array->count++] = value;
}

Value array_get(ArrayObj* array, int index) {
    if (array == NULL || index < 0 || index >= array->count) {
        return value_int(0);
    }
    return array->items[index];
}

void array_set(ArrayObj* array, int index, Value value) {
    if (array == NULL || index < 0 || index >= array->count) return;
    array->items[index] = value;
}

int array_length(ArrayObj* array) {
    if (array == NULL) return 0;
    return array->count;
}
