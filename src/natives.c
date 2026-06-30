#include "natives.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "compiler.h"
#include "os.h"

typedef struct {
    const char* name;
    int arity;
    NativeFn fn;
} NativeDef;

static int native_length(VM* vm, int argc, Value* argv, Value* out) {
    (void)vm;
    (void)argc;
    if (argv[0].type == VAL_ARRAY) {
        *out = value_int(array_length(argv[0].as.as_array));
        return 1;
    }
    if (argv[0].type == VAL_STRING) {
        const char* s = argv[0].as.as_string ? argv[0].as.as_string : "";
        *out = value_int((int)strlen(s));
        return 1;
    }
    vm_set_error(vm, "length expects a string or array");
    return 0;
}

static int native_append(VM* vm, int argc, Value* argv, Value* out) {
    (void)argc;
    if (argv[0].type != VAL_ARRAY) {
        vm_set_error(vm, "append expects an array");
        return 0;
    }
    if (!array_append(argv[0].as.as_array, argv[1])) {
        vm_set_error(vm, "Out of memory");
        return 0;
    }
    *out = argv[0];
    return 1;
}

static int native_println(VM* vm, int argc, Value* argv, Value* out) {
    (void)vm;
    (void)argc;
    value_print(argv[0]);
    printf("\n");
    *out = value_int(0);
    return 1;
}

static int native_clock(VM* vm, int argc, Value* argv, Value* out) {
    (void)vm;
    (void)argc;
    (void)argv;
    *out = value_int((int)(clock() * 1000 / CLOCKS_PER_SEC));
    return 1;
}

static int native_concat(VM* vm, int argc, Value* argv, Value* out) {
    (void)vm;
    (void)argc;
    if (argv[0].type != VAL_STRING || argv[1].type != VAL_STRING) {
        vm_set_error(vm, "concat expects two strings");
        return 0;
    }
    const char* a = argv[0].as.as_string ? argv[0].as.as_string : "";
    const char* b = argv[1].as.as_string ? argv[1].as.as_string : "";
    size_t len = strlen(a) + strlen(b) + 1;
    char* buf = malloc(len);
    if (buf == NULL) {
        vm_set_error(vm, "Out of memory");
        return 0;
    }
    snprintf(buf, len, "%s%s", a, b);
    *out = value_string(buf);
    return 1;
}

static int native_substring(VM* vm, int argc, Value* argv, Value* out) {
    (void)vm;
    (void)argc;
    if (argv[0].type != VAL_STRING || argv[1].type != VAL_INT || argv[2].type != VAL_INT) {
        vm_set_error(vm, "substring expects (string, int, int)");
        return 0;
    }
    const char* s = argv[0].as.as_string ? argv[0].as.as_string : "";
    int len_s = (int)strlen(s);
    int start = argv[1].as.as_int;
    int len = argv[2].as.as_int;
    if (start < 0) start = 0;
    if (start > len_s) start = len_s;
    if (len < 0) len = 0;
    if (start + len > len_s) len = len_s - start;
    char* buf = malloc((size_t)len + 1);
    if (buf == NULL) {
        vm_set_error(vm, "Out of memory");
        return 0;
    }
    memcpy(buf, s + start, len);
    buf[len] = '\0';
    *out = value_string(buf);
    return 1;
}

static int native_contains(VM* vm, int argc, Value* argv, Value* out) {
    (void)vm;
    (void)argc;
    if (argv[0].type != VAL_STRING || argv[1].type != VAL_STRING) {
        vm_set_error(vm, "contains expects two strings");
        return 0;
    }
    const char* hay = argv[0].as.as_string ? argv[0].as.as_string : "";
    const char* needle = argv[1].as.as_string ? argv[1].as.as_string : "";
    *out = value_bool(strstr(hay, needle) != NULL);
    return 1;
}

static int native_index_of(VM* vm, int argc, Value* argv, Value* out) {
    (void)vm;
    (void)argc;
    if (argv[0].type != VAL_STRING || argv[1].type != VAL_STRING) {
        vm_set_error(vm, "index_of expects two strings");
        return 0;
    }
    const char* hay = argv[0].as.as_string ? argv[0].as.as_string : "";
    const char* needle = argv[1].as.as_string ? argv[1].as.as_string : "";
    const char* found = strstr(hay, needle);
    *out = value_int(found != NULL ? (int)(found - hay) : -1);
    return 1;
}

static int native_to_upper(VM* vm, int argc, Value* argv, Value* out) {
    (void)vm;
    (void)argc;
    if (argv[0].type != VAL_STRING) {
        vm_set_error(vm, "to_upper expects a string");
        return 0;
    }
    const char* s = argv[0].as.as_string ? argv[0].as.as_string : "";
    char* buf = strdup(s);
    if (buf == NULL) {
        vm_set_error(vm, "Out of memory");
        return 0;
    }
    for (char* p = buf; *p; p++) {
        if (*p >= 'a' && *p <= 'z') *p -= 32;
    }
    *out = value_string(buf);
    return 1;
}

static int native_to_lower(VM* vm, int argc, Value* argv, Value* out) {
    (void)vm;
    (void)argc;
    if (argv[0].type != VAL_STRING) {
        vm_set_error(vm, "to_lower expects a string");
        return 0;
    }
    const char* s = argv[0].as.as_string ? argv[0].as.as_string : "";
    char* buf = strdup(s);
    if (buf == NULL) {
        vm_set_error(vm, "Out of memory");
        return 0;
    }
    for (char* p = buf; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') *p += 32;
    }
    *out = value_string(buf);
    return 1;
}

static int native_trim(VM* vm, int argc, Value* argv, Value* out) {
    (void)vm;
    (void)argc;
    if (argv[0].type != VAL_STRING) {
        vm_set_error(vm, "trim expects a string");
        return 0;
    }
    const char* s = argv[0].as.as_string ? argv[0].as.as_string : "";
    while (isspace((unsigned char)*s)) s++;
    const char* end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1))) end--;
    size_t len = (size_t)(end - s);
    char* buf = malloc(len + 1);
    if (buf == NULL) {
        vm_set_error(vm, "Out of memory");
        return 0;
    }
    memcpy(buf, s, len);
    buf[len] = '\0';
    *out = value_string(buf);
    return 1;
}

static int native_int_to_string(VM* vm, int argc, Value* argv, Value* out) {
    (void)vm;
    (void)argc;
    if (argv[0].type != VAL_INT) {
        vm_set_error(vm, "int_to_string expects an int");
        return 0;
    }
    char* buf = malloc(32);
    if (buf == NULL) {
        vm_set_error(vm, "Out of memory");
        return 0;
    }
    snprintf(buf, 32, "%d", argv[0].as.as_int);
    *out = value_string(buf);
    return 1;
}

static int native_float_to_string(VM* vm, int argc, Value* argv, Value* out) {
    (void)vm;
    (void)argc;
    if (argv[0].type != VAL_FLOAT) {
        vm_set_error(vm, "float_to_string expects a float");
        return 0;
    }
    double n = argv[0].as.as_float;
    char* buf = malloc(64);
    if (buf == NULL) {
        vm_set_error(vm, "Out of memory");
        return 0;
    }
    snprintf(buf, 64, "%g", n);
    *out = value_string(buf);
    return 1;
}

static int native_abs_int(VM* vm, int argc, Value* argv, Value* out) {
    (void)vm;
    (void)argc;
    if (argv[0].type != VAL_INT) {
        vm_set_error(vm, "abs_int expects an int");
        return 0;
    }
    int n = argv[0].as.as_int;
    if (n == INT_MIN) {
        vm_set_error(vm, "abs_int: absolute value of INT_MIN is undefined");
        return 0;
    }
    *out = value_int(n < 0 ? -n : n);
    return 1;
}

static int native_abs_float(VM* vm, int argc, Value* argv, Value* out) {
    (void)vm;
    (void)argc;
    if (argv[0].type != VAL_FLOAT) {
        vm_set_error(vm, "abs_float expects a float");
        return 0;
    }
    double n = argv[0].as.as_float;
    *out = value_float(n < 0.0 ? -n : n);
    return 1;
}

static int native_min_int(VM* vm, int argc, Value* argv, Value* out) {
    (void)vm;
    (void)argc;
    if (argv[0].type != VAL_INT || argv[1].type != VAL_INT) {
        vm_set_error(vm, "min_int expects two ints");
        return 0;
    }
    int a = argv[0].as.as_int;
    int b = argv[1].as.as_int;
    *out = value_int(a < b ? a : b);
    return 1;
}

static int native_max_int(VM* vm, int argc, Value* argv, Value* out) {
    (void)vm;
    (void)argc;
    if (argv[0].type != VAL_INT || argv[1].type != VAL_INT) {
        vm_set_error(vm, "max_int expects two ints");
        return 0;
    }
    int a = argv[0].as.as_int;
    int b = argv[1].as.as_int;
    *out = value_int(a > b ? a : b);
    return 1;
}

static int native_min_float(VM* vm, int argc, Value* argv, Value* out) {
    (void)vm;
    (void)argc;
    if (argv[0].type != VAL_FLOAT || argv[1].type != VAL_FLOAT) {
        vm_set_error(vm, "min_float expects two floats");
        return 0;
    }
    double a = argv[0].as.as_float;
    double b = argv[1].as.as_float;
    *out = value_float(a < b ? a : b);
    return 1;
}

static int native_max_float(VM* vm, int argc, Value* argv, Value* out) {
    (void)vm;
    (void)argc;
    if (argv[0].type != VAL_FLOAT || argv[1].type != VAL_FLOAT) {
        vm_set_error(vm, "max_float expects two floats");
        return 0;
    }
    double a = argv[0].as.as_float;
    double b = argv[1].as.as_float;
    *out = value_float(a > b ? a : b);
    return 1;
}

static int native_read_file(VM* vm, int argc, Value* argv, Value* out) {
    (void)argc;
    if (argv[0].type != VAL_STRING) {
        vm_set_error(vm, "read_file expects a string path");
        return 0;
    }
    const char* path = argv[0].as.as_string ? argv[0].as.as_string : "";
    char* contents = os_read_file(path);
    if (contents == NULL) {
        vm_set_error(vm, "read_file: could not read file");
        return 0;
    }
    *out = value_string(contents);
    return 1;
}

static int native_write_file(VM* vm, int argc, Value* argv, Value* out) {
    (void)argc;
    if (argv[0].type != VAL_STRING || argv[1].type != VAL_STRING) {
        vm_set_error(vm, "write_file expects (string, string)");
        return 0;
    }
    const char* path = argv[0].as.as_string ? argv[0].as.as_string : "";
    const char* contents = argv[1].as.as_string ? argv[1].as.as_string : "";
    if (!os_write_file(path, contents, strlen(contents))) {
        vm_set_error(vm, "write_file: could not write file");
        return 0;
    }
    *out = value_int(1);
    return 1;
}

static int native_file_exists(VM* vm, int argc, Value* argv, Value* out) {
    (void)vm;
    (void)argc;
    if (argv[0].type != VAL_STRING) {
        vm_set_error(vm, "file_exists expects a string path");
        return 0;
    }
    const char* path = argv[0].as.as_string ? argv[0].as.as_string : "";
    *out = value_bool(os_file_exists(path));
    return 1;
}

static int native_split(VM* vm, int argc, Value* argv, Value* out) {
    (void)argc;
    if (argv[0].type != VAL_STRING || argv[1].type != VAL_STRING) {
        vm_set_error(vm, "split expects (string, string)");
        return 0;
    }
    const char* s = argv[0].as.as_string ? argv[0].as.as_string : "";
    const char* delim = argv[1].as.as_string ? argv[1].as.as_string : "";
    if (strlen(delim) == 0) {
        vm_set_error(vm, "split delimiter cannot be empty");
        return 0;
    }
    ArrayObj* arr = array_new();
    if (arr == NULL) {
        vm_set_error(vm, "Out of memory");
        return 0;
    }
    char* copy = strdup(s);
    if (copy == NULL) {
        vm_set_error(vm, "Out of memory");
        return 0;
    }
    char* token = strtok(copy, delim);
    while (token != NULL) {
        if (!array_append(arr, value_string(strdup(token)))) {
            free(copy);
            vm_set_error(vm, "Out of memory");
            return 0;
        }
        token = strtok(NULL, delim);
    }
    free(copy);
    *out = value_array(arr);
    return 1;
}

static int native_join(VM* vm, int argc, Value* argv, Value* out) {
    (void)argc;
    if (argv[0].type != VAL_ARRAY || argv[1].type != VAL_STRING) {
        vm_set_error(vm, "join expects (array<string>, string)");
        return 0;
    }
    ArrayObj* arr = argv[0].as.as_array;
    const char* delim = argv[1].as.as_string ? argv[1].as.as_string : "";
    size_t delim_len = strlen(delim);
    int count = array_length(arr);
    size_t total = 0;
    for (int i = 0; i < count; i++) {
        Value v = array_get(arr, i);
        if (v.type != VAL_STRING) {
            vm_set_error(vm, "join expects array<string>");
            return 0;
        }
        total += strlen(v.as.as_string ? v.as.as_string : "");
    }
    if (count > 1) {
        total += delim_len * (size_t)(count - 1);
    }
    char* buf = malloc(total + 1);
    if (buf == NULL) {
        vm_set_error(vm, "Out of memory");
        return 0;
    }
    buf[0] = '\0';
    for (int i = 0; i < count; i++) {
        if (i > 0) strcat(buf, delim);
        const char* part = array_get(arr, i).as.as_string;
        strcat(buf, part ? part : "");
    }
    *out = value_string(buf);
    return 1;
}

static int native_replace(VM* vm, int argc, Value* argv, Value* out) {
    (void)argc;
    if (argv[0].type != VAL_STRING || argv[1].type != VAL_STRING || argv[2].type != VAL_STRING) {
        vm_set_error(vm, "replace expects (string, string, string)");
        return 0;
    }
    const char* s = argv[0].as.as_string ? argv[0].as.as_string : "";
    const char* old = argv[1].as.as_string ? argv[1].as.as_string : "";
    const char* new_ = argv[2].as.as_string ? argv[2].as.as_string : "";
    size_t old_len = strlen(old);
    if (old_len == 0) {
        vm_set_error(vm, "replace: old string cannot be empty");
        return 0;
    }
    size_t new_len = strlen(new_);
    size_t count = 0;
    const char* tmp = s;
    const char* match;
    while ((match = strstr(tmp, old)) != NULL) {
        count++;
        tmp = match + old_len;
    }
    size_t result_len = strlen(s) + count * (new_len - old_len);
    char* buf = malloc(result_len + 1);
    if (buf == NULL) {
        vm_set_error(vm, "Out of memory");
        return 0;
    }
    char* dst = buf;
    const char* pos = s;
    while ((match = strstr(pos, old)) != NULL) {
        size_t prefix = (size_t)(match - pos);
        memcpy(dst, pos, prefix);
        dst += prefix;
        memcpy(dst, new_, new_len);
        dst += new_len;
        pos = match + old_len;
    }
    size_t tail = strlen(pos);
    memcpy(dst, pos, tail);
    dst += tail;
    *dst = '\0';
    *out = value_string(buf);
    return 1;
}

static int native_repeat(VM* vm, int argc, Value* argv, Value* out) {
    (void)argc;
    if (argv[0].type != VAL_STRING || argv[1].type != VAL_INT) {
        vm_set_error(vm, "repeat expects (string, int)");
        return 0;
    }
    const char* s = argv[0].as.as_string ? argv[0].as.as_string : "";
    int count = argv[1].as.as_int;
    if (count < 0) count = 0;
    size_t len = strlen(s);
    char* buf = malloc(len * (size_t)count + 1);
    if (buf == NULL) {
        vm_set_error(vm, "Out of memory");
        return 0;
    }
    for (int i = 0; i < count; i++) {
        memcpy(buf + i * len, s, len);
    }
    buf[len * (size_t)count] = '\0';
    *out = value_string(buf);
    return 1;
}

static NativeDef natives[] = {
    {"length",  1, native_length},
    {"append",  2, native_append},
    {"println", 1, native_println},
    {"clock",   0, native_clock},
    {"concat",  2, native_concat},
    {"substring", 3, native_substring},
    {"contains", 2, native_contains},
    {"index_of", 2, native_index_of},
    {"to_upper", 1, native_to_upper},
    {"to_lower", 1, native_to_lower},
    {"trim",    1, native_trim},
    {"int_to_string", 1, native_int_to_string},
    {"float_to_string", 1, native_float_to_string},
    {"abs_int",   1, native_abs_int},
    {"abs_float", 1, native_abs_float},
    {"min_int",   2, native_min_int},
    {"max_int",   2, native_max_int},
    {"min_float", 2, native_min_float},
    {"max_float", 2, native_max_float},
    {"read_file", 1, native_read_file},
    {"write_file", 2, native_write_file},
    {"file_exists", 1, native_file_exists},
    {"split", 2, native_split},
    {"join", 2, native_join},
    {"replace", 3, native_replace},
    {"repeat", 2, native_repeat},
    {NULL,      0, NULL}
};

int native_count(void) {
    int count = 0;
    while (natives[count].name != NULL) count++;
    return count;
}

int native_find(const char* name) {
    for (int i = 0; natives[i].name != NULL; i++) {
        if (strcmp(natives[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int native_arity(int idx) {
    if (idx < 0 || idx >= native_count()) return -1;
    return natives[idx].arity;
}

int native_call(VM* vm, int idx, int argc, Value* argv, Value* out) {
    if (idx < 0 || idx >= native_count()) {
        vm_set_error(vm, "Unknown native function");
        return 0;
    }
    NativeDef* def = &natives[idx];
    if (argc != def->arity) {
        char msg[256];
        snprintf(msg, sizeof(msg), "%s expects %d argument(s)", def->name, def->arity);
        vm_set_error(vm, msg);
        return 0;
    }
    return def->fn(vm, argc, argv, out);
}
