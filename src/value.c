#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "compiler.h"
#include "sql_engine.h"

typedef struct {
    Obj obj;
    char chars[];
} StringObj;

struct ArrayObj {
    Obj obj;
    Value* items;
    int count;
    int capacity;
    ArrayObj* next;
};

struct RowObj {
    Obj obj;
    int column_count;
    char** column_names;
    Value* column_values;
};

CursorObj* cursor_obj_new(DBDriver* driver) {
    CursorObj* cursor = malloc(sizeof(CursorObj));
    if (cursor == NULL) return NULL;
    cursor->obj.type = OBJ_CURSOR;
    cursor->obj.ref_count = 1;
    cursor->driver = driver;
    cursor->result_handle = NULL;
    cursor->row_handle = NULL;
    cursor->is_open = 0;
    cursor->row_count = 0;
    cursor->found = 0;
    return cursor;
}

static ArrayObj* array_pool = NULL;

void row_obj_free(RowObj* row);

static StringObj* string_obj_from_chars(const char* chars) {
    if (chars == NULL) return NULL;
    return (StringObj*)(chars - offsetof(StringObj, chars));
}

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
    if (s == NULL) {
        value.as.as_string = NULL;
        return value;
    }
    size_t len = strlen(s);
    StringObj* obj = malloc(sizeof(StringObj) + len + 1);
    if (obj == NULL) {
        value.as.as_string = NULL;
        free(s);
        return value;
    }
    obj->obj.type = OBJ_STRING;
    obj->obj.ref_count = 1;
    memcpy(obj->chars, s, len + 1);
    free(s);
    value.as.as_string = obj->chars;
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

Value value_map(MapObj* map) {
    Value value;
    value.type = VAL_MAP;
    value.as.as_map = map;
    return value;
}

Value value_row(RowObj* row) {
    Value value;
    value.type = VAL_ROW;
    value.as.as_row_handle = row;
    return value;
}

Value value_cursor(CursorObj* cursor) {
    Value value;
    value.type = VAL_CURSOR;
    value.as.as_cursor = cursor;
    return value;
}

void value_retain(Value v) {
    if (v.type == VAL_STRING && v.as.as_string != NULL) {
        string_obj_from_chars(v.as.as_string)->obj.ref_count++;
    } else if (v.type == VAL_ARRAY && v.as.as_array != NULL) {
        v.as.as_array->obj.ref_count++;
    } else if (v.type == VAL_MAP && v.as.as_map != NULL) {
        v.as.as_map->obj.ref_count++;
    } else if (v.type == VAL_ROW && v.as.as_row_handle != NULL) {
        ((RowObj*)v.as.as_row_handle)->obj.ref_count++;
    } else if (v.type == VAL_CURSOR && v.as.as_cursor != NULL) {
        v.as.as_cursor->obj.ref_count++;
    }
}

static void cursor_obj_free(CursorObj* cursor) {
    if (cursor == NULL) return;
    if (cursor->result_handle != NULL && cursor->driver != NULL) {
        cursor->driver->result_free(cursor->driver, cursor->result_handle);
    }
    free(cursor);
}

void value_release(Value v) {
    if (v.type == VAL_STRING && v.as.as_string != NULL) {
        StringObj* obj = string_obj_from_chars(v.as.as_string);
        if (--obj->obj.ref_count <= 0) {
            free(obj);
        }
    } else if (v.type == VAL_ARRAY && v.as.as_array != NULL) {
        ArrayObj* array = v.as.as_array;
        if (--array->obj.ref_count <= 0) {
            array_free(array);
        }
    } else if (v.type == VAL_MAP && v.as.as_map != NULL) {
        MapObj* map = v.as.as_map;
        if (--map->obj.ref_count <= 0) {
            map_free(map);
        }
    } else if (v.type == VAL_ROW && v.as.as_row_handle != NULL) {
        RowObj* row = (RowObj*)v.as.as_row_handle;
        if (--row->obj.ref_count <= 0) {
            row_obj_free(row);
        }
    } else if (v.type == VAL_CURSOR && v.as.as_cursor != NULL) {
        CursorObj* cursor = v.as.as_cursor;
        if (--cursor->obj.ref_count <= 0) {
            cursor_obj_free(cursor);
        }
    }
}

int value_ref_count(Value v) {
    if (v.type == VAL_STRING && v.as.as_string != NULL) {
        return string_obj_from_chars(v.as.as_string)->obj.ref_count;
    }
    if (v.type == VAL_ARRAY && v.as.as_array != NULL) {
        return v.as.as_array->obj.ref_count;
    }
    if (v.type == VAL_MAP && v.as.as_map != NULL) {
        return v.as.as_map->obj.ref_count;
    }
    if (v.type == VAL_ROW && v.as.as_row_handle != NULL) {
        return ((RowObj*)v.as.as_row_handle)->obj.ref_count;
    }
    if (v.type == VAL_CURSOR && v.as.as_cursor != NULL) {
        return v.as.as_cursor->obj.ref_count;
    }
    return 0;
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
    if (a.type == VAL_STRING && b.type == VAL_STRING) {
        const char* as = a.as.as_string ? a.as.as_string : "";
        const char* bs = b.as.as_string ? b.as.as_string : "";
        size_t len = strlen(as) + strlen(bs) + 1;
        char* buf = malloc(len);
        if (buf == NULL) return value_int(0);
        snprintf(buf, len, "%s%s", as, bs);
        return value_string(buf);
    }
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
    if (a.type == VAL_BOOL && b.type == VAL_BOOL) {
        return value_int(a.as.as_int == b.as.as_int ? 1 : 0);
    }
    if (a.type == VAL_BOOL || b.type == VAL_BOOL) {
        return value_int(0);
    }
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
    if (a.type == VAL_STRING && b.type == VAL_STRING) {
        const char* as = a.as.as_string ? a.as.as_string : "";
        const char* bs = b.as.as_string ? b.as.as_string : "";
        return value_int(strcmp(as, bs) < 0 ? 1 : 0);
    }
    if (a.type == VAL_BOOL && b.type == VAL_BOOL) {
        return value_int(a.as.as_int < b.as.as_int ? 1 : 0);
    }
    if (a.type == VAL_BOOL || b.type == VAL_BOOL) {
        return value_int(0);
    }
    if (either_float(a, b)) {
        return value_int(as_number(a) < as_number(b) ? 1 : 0);
    }
    if (a.type == VAL_INT && b.type == VAL_INT) {
        return value_int(a.as.as_int < b.as.as_int ? 1 : 0);
    }
    return value_int(0);
}

Value value_gt(Value a, Value b) {
    if (a.type == VAL_STRING && b.type == VAL_STRING) {
        const char* as = a.as.as_string ? a.as.as_string : "";
        const char* bs = b.as.as_string ? b.as.as_string : "";
        return value_int(strcmp(as, bs) > 0 ? 1 : 0);
    }
    if (a.type == VAL_BOOL && b.type == VAL_BOOL) {
        return value_int(a.as.as_int > b.as.as_int ? 1 : 0);
    }
    if (a.type == VAL_BOOL || b.type == VAL_BOOL) {
        return value_int(0);
    }
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
        case VAL_MAP:
            return value.as.as_map != NULL;
        case VAL_CURSOR:
            return value.as.as_cursor != NULL;
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
        case VAL_MAP: {
            MapObj* map = value.as.as_map;
            printf("{");
            if (map != NULL) {
                for (int i = 0; i < map->count; i++) {
                    value_print(map->keys[i]);
                    printf(": ");
                    value_print(map->values[i]);
                    if (i < map->count - 1) printf(", ");
                }
            }
            printf("}");
            break;
        }
        case VAL_ROW:
            printf("(row)");
            break;
        case VAL_CURSOR:
            printf("(cursor)");
            break;
        default:
            printf("?");
            break;
    }
}

ArrayObj* array_new(void) {
    ArrayObj* array = malloc(sizeof(ArrayObj));
    if (array == NULL) return NULL;
    array->obj.type = OBJ_ARRAY;
    array->obj.ref_count = 1;
    array->items = NULL;
    array->count = 0;
    array->capacity = 0;
    array->next = array_pool;
    array_pool = array;
    return array;
}

void array_free(ArrayObj* array) {
    if (array == NULL) return;
    ArrayObj** current = &array_pool;
    while (*current != NULL) {
        if (*current == array) {
            *current = array->next;
            break;
        }
        current = &(*current)->next;
    }
    for (int i = 0; i < array->count; i++) {
        value_release(array->items[i]);
    }
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

RowObj* row_obj_new(int column_count) {
    RowObj* row = malloc(sizeof(RowObj));
    if (row == NULL) return NULL;
    row->obj.type = OBJ_ROW;
    row->obj.ref_count = 1;
    row->column_count = column_count;
    row->column_names = calloc((size_t)column_count, sizeof(char*));
    if (row->column_names == NULL && column_count > 0) {
        free(row);
        return NULL;
    }
    row->column_values = calloc((size_t)column_count, sizeof(Value));
    if (row->column_values == NULL && column_count > 0) {
        free(row->column_names);
        free(row);
        return NULL;
    }
    return row;
}

void row_obj_free(RowObj* row) {
    if (row == NULL) return;
    for (int i = 0; i < row->column_count; i++) {
        free(row->column_names[i]);
        value_release(row->column_values[i]);
    }
    free(row->column_names);
    free(row->column_values);
    free(row);
}

void row_obj_set_column(RowObj* row, int index, const char* name, Value value) {
    if (row == NULL || index < 0 || index >= row->column_count) return;
    free(row->column_names[index]);
    row->column_names[index] = name != NULL ? strdup(name) : NULL;
    value_release(row->column_values[index]);
    row->column_values[index] = value;
    value_retain(value);
}

Value row_obj_get_field(RowObj* row, const char* name) {
    if (row == NULL || name == NULL) return value_int(0);
    for (int i = 0; i < row->column_count; i++) {
        if (row->column_names[i] != NULL && strcmp(row->column_names[i], name) == 0) {
            Value v = row->column_values[i];
            value_retain(v);
            return v;
        }
    }
    return value_int(0);
}

int row_obj_set_field(RowObj* row, const char* name, Value value) {
    if (row == NULL || name == NULL) return 0;
    for (int i = 0; i < row->column_count; i++) {
        if (row->column_names[i] != NULL && strcmp(row->column_names[i], name) == 0) {
            row_obj_set_column(row, i, name, value);
            return 1;
        }
    }
    return 0;
}

int array_append(ArrayObj* array, Value value) {
    if (array->count >= array->capacity) {
        int new_capacity = array->capacity == 0 ? 4 : array->capacity * 2;
        Value* new_items = realloc(array->items, sizeof(Value) * (size_t)new_capacity);
        if (new_items == NULL) return 0;
        array->items = new_items;
        array->capacity = new_capacity;
    }
    value_retain(value);
    array->items[array->count++] = value;
    return 1;
}

Value array_get(ArrayObj* array, int index) {
    if (array == NULL || index < 0 || index >= array->count) {
        return value_int(0);
    }
    return array->items[index];
}

void array_set(ArrayObj* array, int index, Value value) {
    if (array == NULL || index < 0 || index >= array->count) return;
    value_retain(value);
    value_release(array->items[index]);
    array->items[index] = value;
}

int array_length(ArrayObj* array) {
    if (array == NULL) return 0;
    return array->count;
}

int array_extend(ArrayObj* array, int count) {
    if (array == NULL || count < 0) return 0;
    for (int i = 0; i < count; i++) {
        if (!array_append(array, value_int(0))) return 0;
    }
    return 1;
}

int array_trim(ArrayObj* array, int count) {
    if (array == NULL || count < 0) return 0;
    if (count > array->count) count = array->count;
    for (int i = array->count - count; i < array->count; i++) {
        value_release(array->items[i]);
    }
    array->count -= count;
    return 1;
}

static int values_equal_values(Value a, Value b) {
    if (a.type != b.type) return 0;
    switch (a.type) {
        case VAL_INT:    return a.as.as_int == b.as.as_int;
        case VAL_FLOAT:  return a.as.as_float == b.as.as_float;
        case VAL_BOOL:   return a.as.as_int == b.as.as_int;
        case VAL_STRING: {
            const char* as = a.as.as_string ? a.as.as_string : "";
            const char* bs = b.as.as_string ? b.as.as_string : "";
            return strcmp(as, bs) == 0;
        }
        case VAL_ARRAY:  return a.as.as_array == b.as.as_array;
        case VAL_MAP:    return a.as.as_map == b.as.as_map;
        case VAL_ROW:    return a.as.as_row_handle == b.as.as_row_handle;
        case VAL_CURSOR: return a.as.as_cursor == b.as.as_cursor;
    }
    return 0;
}

MapObj* map_new(void) {
    MapObj* map = malloc(sizeof(MapObj));
    if (map == NULL) return NULL;
    map->obj.type = OBJ_MAP;
    map->obj.ref_count = 1;
    map->keys = NULL;
    map->values = NULL;
    map->count = 0;
    map->capacity = 0;
    return map;
}

void map_free(MapObj* map) {
    if (map == NULL) return;
    for (int i = 0; i < map->count; i++) {
        value_release(map->keys[i]);
        value_release(map->values[i]);
    }
    free(map->keys);
    free(map->values);
    free(map);
}

static int map_find_key(MapObj* map, Value key) {
    for (int i = 0; i < map->count; i++) {
        if (values_equal_values(map->keys[i], key)) return i;
    }
    return -1;
}

int map_set(MapObj* map, Value key, Value value) {
    if (map == NULL) return 0;
    if (key.type != VAL_INT && key.type != VAL_STRING) return 0;
    int idx = map_find_key(map, key);
    if (idx >= 0) {
        value_release(map->values[idx]);
        map->values[idx] = value;
        value_retain(value);
        return 1;
    }
    if (map->count >= map->capacity) {
        int new_capacity = map->capacity == 0 ? 4 : map->capacity * 2;
        Value* new_keys = realloc(map->keys, sizeof(Value) * (size_t)new_capacity);
        Value* new_values = realloc(map->values, sizeof(Value) * (size_t)new_capacity);
        if (new_keys == NULL || new_values == NULL) {
            free(new_keys);
            free(new_values);
            return 0;
        }
        map->keys = new_keys;
        map->values = new_values;
        map->capacity = new_capacity;
    }
    value_retain(key);
    value_retain(value);
    map->keys[map->count] = key;
    map->values[map->count] = value;
    map->count++;
    return 1;
}

int map_get(MapObj* map, Value key, Value* out) {
    if (map == NULL || out == NULL) return 0;
    int idx = map_find_key(map, key);
    if (idx < 0) return 0;
    *out = map->values[idx];
    value_retain(*out);
    return 1;
}

int map_delete(MapObj* map, Value key) {
    if (map == NULL) return 0;
    int idx = map_find_key(map, key);
    if (idx < 0) return 0;
    value_release(map->keys[idx]);
    value_release(map->values[idx]);
    map->count--;
    for (int i = idx; i < map->count; i++) {
        map->keys[i] = map->keys[i + 1];
        map->values[i] = map->values[i + 1];
    }
    return 1;
}

int map_count(MapObj* map) {
    if (map == NULL) return 0;
    return map->count;
}

static int map_key_compare(Value a, Value b) {
    if (a.type == VAL_INT && b.type == VAL_INT) {
        return a.as.as_int - b.as.as_int;
    }
    if (a.type == VAL_STRING && b.type == VAL_STRING) {
        const char* as = a.as.as_string ? a.as.as_string : "";
        const char* bs = b.as.as_string ? b.as.as_string : "";
        return strcmp(as, bs);
    }
    return 0;
}

int map_first_key(MapObj* map, Value* out) {
    if (map == NULL || map->count == 0 || out == NULL) return 0;
    Value min = map->keys[0];
    for (int i = 1; i < map->count; i++) {
        if (map_key_compare(map->keys[i], min) < 0) min = map->keys[i];
    }
    *out = min;
    value_retain(*out);
    return 1;
}

int map_last_key(MapObj* map, Value* out) {
    if (map == NULL || map->count == 0 || out == NULL) return 0;
    Value max = map->keys[0];
    for (int i = 1; i < map->count; i++) {
        if (map_key_compare(map->keys[i], max) > 0) max = map->keys[i];
    }
    *out = max;
    value_retain(*out);
    return 1;
}

int map_next_key(MapObj* map, Value key, Value* out) {
    if (map == NULL || map->count == 0 || out == NULL) return 0;
    Value candidate;
    int found = 0;
    for (int i = 0; i < map->count; i++) {
        if (map_key_compare(map->keys[i], key) > 0) {
            if (!found || map_key_compare(map->keys[i], candidate) < 0) {
                candidate = map->keys[i];
                found = 1;
            }
        }
    }
    if (!found) return 0;
    *out = candidate;
    value_retain(*out);
    return 1;
}

int map_prior_key(MapObj* map, Value key, Value* out) {
    if (map == NULL || map->count == 0 || out == NULL) return 0;
    Value candidate;
    int found = 0;
    for (int i = 0; i < map->count; i++) {
        if (map_key_compare(map->keys[i], key) < 0) {
            if (!found || map_key_compare(map->keys[i], candidate) > 0) {
                candidate = map->keys[i];
                found = 1;
            }
        }
    }
    if (!found) return 0;
    *out = candidate;
    value_retain(*out);
    return 1;
}
