#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "repl.h"
#include "compiler.h"
#include "vm.h"

#define LINE_SIZE 1024

static int run_source(const char* source) {
    Chunk chunk;
    init_chunk(&chunk);
    if (!compile(source, &chunk)) {
        fprintf(stderr, "Compile error\n");
        free_chunk(&chunk);
        return 1;
    }

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

void repl_run(void) {
    char line[LINE_SIZE];
    printf("MyPL REPL (type 'exit' to quit)\n");
    for (;;) {
        printf("> ");
        if (fgets(line, LINE_SIZE, stdin) == NULL) {
            printf("\n");
            break;
        }

        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) {
            break;
        }
        if (strlen(line) == 0) {
            continue;
        }

        char wrapped[LINE_SIZE + 64];
        snprintf(wrapped, sizeof(wrapped), "proc main() -> int { return %s; }", line);
        run_source(wrapped);
    }
}
