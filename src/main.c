#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mydb.h"
#include "repl.h"
#include "vm.h"
#include "compiler.h"
#include "os.h"

static int run_file(const char* path) {
    char* source = os_read_file(path);
    if (source == NULL) {
        fprintf(stderr, "Could not read file: %s\n", path);
        return 1;
    }

    Chunk chunk;
    init_chunk(&chunk);
    if (!compile(source, &chunk)) {
        fprintf(stderr, "Compile error\n");
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
        fprintf(stderr, "Runtime error\n");
        rc = 1;
    }

    vm_free(vm);
    free_chunk(&chunk);
    return rc;
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    if (argc == 2) {
        return run_file(argv[1]);
    }
    if (argc == 1) {
        repl_run();
        return 0;
    }

    fprintf(stderr, "Usage: %s [file]\n", argv[0]);
    return 1;
}
