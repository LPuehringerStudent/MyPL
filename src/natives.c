#include "natives.h"

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

static NativeDef natives[] = {
    {"length",  1, native_length},
    {"append",  2, native_append},
    {"println", 1, native_println},
    {"clock",   0, native_clock},
    {"concat",  2, native_concat},
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
