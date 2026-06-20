#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mydb.h"
#include "repl.h"
#include "vm.h"
#include "compiler.h"
#include "os.h"

static void print_usage(const char* program) {
    fprintf(stderr, "Usage: %s [file]\n", program);
    fprintf(stderr, "\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --help     Show this help message\n");
    fprintf(stderr, "  --version  Show version information\n");
}

static void print_version(void) {
    printf("MyPL 0.1.0\n");
}

static int run_file(const char* path) {
    char* source = os_read_file(path);
    if (source == NULL) {
        fprintf(stderr, "Could not read file: %s\n", path);
        return 1;
    }

    Chunk chunk;
    init_chunk(&chunk);
    char error[256];
    if (!compile(source, &chunk, error, sizeof(error))) {
        fprintf(stderr, "Compile error: %s\n", error[0] != '\0' ? error : "unknown error");
        free(source);
        free_chunk(&chunk);
        return 1;
    }
    free(source);

    VM* vm = vm_init();
    if (vm == NULL) {
        fprintf(stderr, "Out of memory\n");
        free_chunk(&chunk);
        return 1;
    }

    InterpretResult result = vm_interpret(vm, &chunk);
    int rc = 0;
    if (result == INTERPRET_OK) {
        Value v = vm_pop(vm);
        value_print(v);
        printf("\n");
    } else {
        const char* err = vm_get_error(vm);
        fprintf(stderr, "Runtime error: %s\n", err != NULL ? err : "unknown error");
        rc = 1;
    }

    vm_free(vm);
    free_chunk(&chunk);
    return rc;
}

int main(int argc, char** argv) {
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        print_version();
        return 0;
    }

    if (argc == 2) {
        return run_file(argv[1]);
    }
    if (argc == 1) {
        repl_run();
        return 0;
    }

    print_usage(argv[0]);
    return 1;
}
