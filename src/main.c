#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mydb.h"
#include "repl.h"
#include "vm.h"
#include "compiler.h"
#include "os.h"
#include "sql_engine.h"
#include "sqlite_driver.h"

static void print_usage(const char* program) {
    fprintf(stderr, "Usage: %s [file] [--db <path>]\n", program);
    fprintf(stderr, "\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --help     Show this help message\n");
    fprintf(stderr, "  --version  Show version information\n");
    fprintf(stderr, "  --db       Open a SQLite database\n");
}

static void print_version(void) {
    printf("MyPL 0.1.0\n");
}

static int run_file(const char* path, DBDriver* driver) {
    char* source = os_read_file(path);
    if (source == NULL) {
        fprintf(stderr, "Could not read file: %s\n", path);
        return 1;
    }

    Chunk chunk;
    init_chunk(&chunk);
    char error[256];
    if (!compile_with_context_and_path(source, &chunk, path, error, sizeof(error), NULL)) {
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

    if (driver != NULL) {
        vm_set_driver(vm, driver);
    }

    InterpretResult result = vm_interpret(vm, &chunk);
    int rc = 0;
    if (result == INTERPRET_OK) {
        Value v = vm_pop(vm);
        value_print(v);
        printf("\n");
        value_release(v);
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
    if (argc >= 2 && strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--version") == 0) {
        print_version();
        return 0;
    }

    const char* file = NULL;
    const char* db_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--db") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing argument for --db\n");
                print_usage(argv[0]);
                return 1;
            }
            db_path = argv[++i];
        } else if (file == NULL) {
            file = argv[i];
        } else {
            fprintf(stderr, "Unexpected argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    DBDriver driver;
    int driver_open = 0;
    if (db_path != NULL) {
        sqlite_driver_init(&driver);
        if (!driver.open(&driver, db_path)) {
            fprintf(stderr, "Could not open database: %s\n", db_path);
            return 1;
        }
        driver_open = 1;
    } else if (file != NULL) {
        custom_driver_init(&driver);
        if (!driver.open(&driver, "mypl.db")) {
            fprintf(stderr, "Could not open database: mypl.db\n");
            return 1;
        }
        driver_open = 1;
    }

    int rc = 0;
    if (file != NULL) {
        rc = run_file(file, driver_open ? &driver : NULL);
    } else {
        repl_run(db_path);
    }

    if (driver_open) {
        driver.close(&driver);
    }
    return rc;
}
