#include "natives.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "compiler.h"

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
    *out = value_int(strstr(hay, needle) != NULL ? 1 : 0);
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
    double n;
    if (argv[0].type == VAL_FLOAT) {
        n = argv[0].as.as_float;
    } else if (argv[0].type == VAL_INT) {
        n = (double)argv[0].as.as_int;
    } else {
        vm_set_error(vm, "float_to_string expects a float");
        return 0;
    }
    char* buf = malloc(64);
    if (buf == NULL) {
        vm_set_error(vm, "Out of memory");
        return 0;
    }
    snprintf(buf, 64, "%g", n);
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
