#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mydb.h"
#include "repl.h"
#include "vm.h"
#include "compiler.h"
#include "os.h"
#include "packages.h"
#include "stored_programs.h"
#include "sql_engine.h"
#ifdef USE_SQLITE
#include "sqlite_driver.h"
#endif

static void print_usage(const char* program) {
    fprintf(stderr, "Usage: %s [file] [--db <path>]\n", program);
    fprintf(stderr, "\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --help     Show this help message\n");
    fprintf(stderr, "  --version  Show version information\n");
#ifdef USE_SQLITE
    fprintf(stderr, "  --db       Open a SQLite database\n");
#else
    fprintf(stderr, "  --db       (disabled in this build)\n");
#endif
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

    Context* ctx = NULL;
    Context custom_ctx;
    if (driver != NULL && !driver->is_sqlite) {
        custom_ctx.db_path = "mypl.db";
        custom_ctx.pager = NULL;
        ctx = &custom_ctx;
    }

    char* builtin_source = packages_load_builtins();
    char* package_source = driver != NULL ? packages_load_source(driver, ctx) : NULL;
    char* program_source = driver != NULL ? stored_programs_load_source(driver, ctx) : NULL;
    if (program_source != NULL && source != NULL) {
        char* filtered = stored_programs_filter_redefined(program_source, source);
        free(program_source);
        program_source = filtered;
    }

    size_t builtin_len = (builtin_source != NULL && *builtin_source != '\0') ? strlen(builtin_source) : 0;
    size_t pkg_len = (package_source != NULL && *package_source != '\0') ? strlen(package_source) : 0;
    size_t prg_len = (program_source != NULL && *program_source != '\0') ? strlen(program_source) : 0;
    size_t src_len = strlen(source);

    char* combined = NULL;
    if (builtin_len > 0 || pkg_len > 0 || prg_len > 0) {
        combined = malloc(builtin_len + pkg_len + prg_len + src_len + 4);
        if (combined != NULL) {
            size_t off = 0;
            if (builtin_len > 0) {
                memcpy(combined + off, builtin_source, builtin_len);
                off += builtin_len;
                combined[off++] = '\n';
            }
            if (pkg_len > 0) {
                memcpy(combined + off, package_source, pkg_len);
                off += pkg_len;
                combined[off++] = '\n';
            }
            if (prg_len > 0) {
                memcpy(combined + off, program_source, prg_len);
                off += prg_len;
                combined[off++] = '\n';
            }
            memcpy(combined + off, source, src_len + 1);
        }
    }

    free(builtin_source);
    free(package_source);
    free(program_source);
    const char* compile_source = combined != NULL ? combined : source;

    Chunk chunk;
    init_chunk(&chunk);
    char error[256];
    if (!compile_with_context_and_path(compile_source, &chunk, path, error, sizeof(error), ctx)) {
        fprintf(stderr, "Compile error: %s\n", error[0] != '\0' ? error : "unknown error");
        free(source);
        free(combined);
        free_chunk(&chunk);
        return 1;
    }

    if (driver != NULL && strstr(source, "package ") != NULL) {
        packages_save_source(driver, ctx, source, 0);
    }
    if (driver != NULL) {
        stored_programs_save_source(driver, ctx, source);
    }

    free(source);
    free(combined);

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
#ifdef USE_SQLITE
        sqlite_driver_init(&driver);
        if (!driver.open(&driver, db_path)) {
            fprintf(stderr, "Could not open database: %s\n", db_path);
            return 1;
        }
        driver_open = 1;
#else
        fprintf(stderr, "SQLite support is disabled in this build\n");
        return 1;
#endif
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
