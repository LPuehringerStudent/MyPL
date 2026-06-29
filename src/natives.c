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

static NativeDef natives[] = {
    {"length",  1, native_length},
    {"append",  2, native_append},
    {"println", 1, native_println},
    {"clock",   0, native_clock},
    {"concat",  2, native_concat},
    {"substring", 3, native_substring},
    {"contains", 2, native_contains},
    {"index_of", 2, native_index_of},
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
