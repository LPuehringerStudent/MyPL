#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "repl.h"
#include "compiler.h"
#include "os.h"
#include "sql_engine.h"
#ifdef USE_SQLITE
#include "sqlite_driver.h"
#endif
#include "vm.h"

#define LINE_SIZE 1024
#define DEFAULT_DB_PATH "mypl.db"
#define MAX_REPL_VARS 256

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} StringBuffer;

typedef struct {
    StringBuffer procedures;
    StringBuffer main_body;
    char* var_names[MAX_REPL_VARS];
    int var_count;
    Chunk chunk;
    VM* vm;
    Context ctx;
    DBDriver driver;
    int driver_open;
} ReplSession;

static void string_buffer_init(StringBuffer* buf) {
    buf->data = malloc(256);
    buf->len = 0;
    buf->cap = 256;
    if (buf->data != NULL) buf->data[0] = '\0';
}

static void string_buffer_free(StringBuffer* buf) {
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

static int string_buffer_append(StringBuffer* buf, const char* text) {
    if (buf->data == NULL || text == NULL) return 0;
    size_t text_len = strlen(text);
    size_t needed = buf->len + text_len + 1;
    if (needed > buf->cap) {
        size_t new_cap = buf->cap * 2;
        while (new_cap < needed) new_cap *= 2;
        char* new_data = realloc(buf->data, new_cap);
        if (new_data == NULL) return 0;
        buf->data = new_data;
        buf->cap = new_cap;
    }
    memcpy(buf->data + buf->len, text, text_len + 1);
    buf->len += text_len;
    return 1;
}

static void repl_session_init(ReplSession* session, const char* db_path) {
    string_buffer_init(&session->procedures);
    string_buffer_init(&session->main_body);
    session->var_count = 0;
    init_chunk(&session->chunk);
    session->vm = vm_init();
    session->driver_open = 0;
    session->ctx.db_path = DEFAULT_DB_PATH;
    session->ctx.pager = NULL;

    if (db_path != NULL) {
#ifdef USE_SQLITE
        sqlite_driver_init(&session->driver);
        if (session->driver.open(&session->driver, db_path)) {
            session->driver_open = 1;
            if (session->vm != NULL) {
                vm_set_driver(session->vm, &session->driver);
            }
        } else {
            fprintf(stderr, "Could not open database: %s\n", db_path);
        }
#else
        fprintf(stderr, "SQLite support is disabled in this build\n");
#endif
    } else {
        catalog_open(&session->ctx);
        if (session->vm != NULL) {
            vm_set_context(session->vm, &session->ctx);
        }
    }
}

static void repl_session_free(ReplSession* session) {
    string_buffer_free(&session->procedures);
    string_buffer_free(&session->main_body);
    for (int i = 0; i < session->var_count; i++) {
        free(session->var_names[i]);
    }
    session->var_count = 0;
    if (session->vm != NULL) {
        vm_free(session->vm);
        session->vm = NULL;
    }
    free_chunk(&session->chunk);
    if (session->driver_open) {
        session->driver.close(&session->driver);
        session->driver_open = 0;
    } else {
        catalog_close(&session->ctx);
    }
}

static void trim_trailing_ws(char* s) {
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[len - 1] = '\0';
        len--;
    }
}

static int is_procedure_definition(const char* line) {
    while (*line != '\0' && isspace((unsigned char)*line)) line++;
    return strncmp(line, "proc ", 5) == 0;
}

static int is_statement(const char* line) {
    size_t len = strlen(line);
    return len > 0 && line[len - 1] == ';';
}

static char* extract_var_name(const char* line) {
    const char* p = line;
    while (*p != '\0' && isspace((unsigned char)*p)) p++;

    /* Skip type keyword (e.g. int, float, string, bool, array<int>). */
    if (!isalpha((unsigned char)*p)) return NULL;
    while (*p != '\0' && (isalnum((unsigned char)*p) || *p == '<' || *p == '>' || *p == ' ')) {
        if (*p == ' ') {
            p++;
            break;
        }
        p++;
    }
    while (*p != '\0' && isspace((unsigned char)*p)) p++;

    const char* start = p;
    while (*p != '\0' && !isspace((unsigned char)*p) && *p != ':' && *p != '=' && *p != ';') p++;
    if (p == start) return NULL;
    size_t len = (size_t)(p - start);
    char* name = malloc(len + 1);
    if (name == NULL) return NULL;
    memcpy(name, start, len);
    name[len] = '\0';
    return name;
}

static void record_var_name(ReplSession* session, const char* line) {
    char* name = extract_var_name(line);
    if (name == NULL) return;
    if (session->var_count >= MAX_REPL_VARS) {
        free(name);
        return;
    }
    session->var_names[session->var_count++] = name;
}

static int compose_source(ReplSession* session, const char* current_line,
                          int current_is_expr, StringBuffer* out) {
    string_buffer_init(out);
    if (out->data == NULL) return 0;

    if (!string_buffer_append(out, session->procedures.data)) return 0;
    if (session->procedures.len > 0 &&
        !string_buffer_append(out, "\n")) return 0;

    if (!string_buffer_append(out, "proc main() -> int { ")) return 0;
    if (session->main_body.len > 0) {
        if (!string_buffer_append(out, session->main_body.data)) return 0;
        if (!string_buffer_append(out, " ")) return 0;
    }
    if (current_is_expr) {
        if (!string_buffer_append(out, "return ")) return 0;
        if (!string_buffer_append(out, current_line)) return 0;
        if (!string_buffer_append(out, ";")) return 0;
    } else {
        if (!string_buffer_append(out, "return 0;")) return 0;
    }
    if (!string_buffer_append(out, " }\n")) return 0;
    return 1;
}

static int run_source(ReplSession* session, const char* source) {
    free_chunk(&session->chunk);
    init_chunk(&session->chunk);
    char error[256];
    Context* ctx = session->driver_open ? NULL : &session->ctx;
    if (!compile_with_context_and_path(source, &session->chunk, NULL,
                                       error, sizeof(error), ctx)) {
        fprintf(stderr, "Compile error: %s\n", error[0] != '\0' ? error : "unknown error");
        return 1;
    }

    if (session->vm == NULL) {
        session->vm = vm_init();
        if (session->vm != NULL) {
            if (session->driver_open) {
                vm_set_driver(session->vm, &session->driver);
            } else {
                vm_set_context(session->vm, &session->ctx);
            }
        }
    }
    if (session->vm == NULL) {
        fprintf(stderr, "Out of memory\n");
        return 1;
    }

    InterpretResult result = vm_interpret(session->vm, &session->chunk);
    int rc = 0;
    if (result == INTERPRET_OK) {
        Value v = vm_pop(session->vm);
        value_print(v);
        printf("\n");
        value_release(v);
    } else {
        const char* err = vm_get_error(session->vm);
        fprintf(stderr, "Runtime error: %s\n", err != NULL ? err : "unknown error");
        rc = 1;
    }
    return rc;
}

static int run_current_line(ReplSession* session, const char* line) {
    StringBuffer composed;
    int is_expr = !is_statement(line);
    if (!compose_source(session, line, is_expr, &composed)) {
        fprintf(stderr, "Out of memory\n");
        return 1;
    }
    int rc = run_source(session, composed.data);
    string_buffer_free(&composed);
    return rc;
}

static int cmd_load(ReplSession* session, const char* path) {
    if (path == NULL || *path == '\0') {
        fprintf(stderr, "Usage: .load <file>\n");
        return 1;
    }
    char* source = os_read_file(path);
    if (source == NULL) {
        fprintf(stderr, "Could not read file: %s\n", path);
        return 1;
    }

    /* Append any procedure definitions found in the file so they remain
       available for later REPL input. */
    const char* p = source;
    while (*p != '\0') {
        while (*p != '\0' && isspace((unsigned char)*p)) p++;
        if (*p == '\0') break;
        if (strncmp(p, "proc ", 5) == 0) {
            const char* start = p;
            while (*p != '\0' && *p != '}') p++;
            if (*p == '}') p++;
            size_t len = (size_t)(p - start);
            char* def = malloc(len + 1);
            if (def != NULL) {
                memcpy(def, start, len);
                def[len] = '\0';
                string_buffer_append(&session->procedures, def);
                string_buffer_append(&session->procedures, "\n");
                free(def);
            }
        } else {
            p++;
        }
    }

    /* Run the loaded source. If the file has no main procedure, validate the
       extracted procedure definitions with a synthetic main. */
    int has_main = strstr(source, "proc main") != NULL;
    if (has_main) {
        int rc = run_source(session, source);
        free(source);
        return rc;
    }
    StringBuffer validation;
    string_buffer_init(&validation);
    if (validation.data != NULL) {
        string_buffer_append(&validation, session->procedures.data);
        string_buffer_append(&validation, "proc main() -> int { return 0; }\n");
        run_source(session, validation.data);
        string_buffer_free(&validation);
    }
    free(source);
    return 0;
}

static void print_value(Value v) {
    value_print(v);
}

static void print_driver_row(ReplSession* session, void* result, void* row, int col_count) {
    for (int i = 0; i < col_count; i++) {
        if (i > 0) printf(" | ");
        const char* name = session->driver.result_column_name(&session->driver, result, i);
        if (name == NULL) name = "?";
        Value field;
        if (session->driver.row_get_field(&session->driver, row, name, &field)) {
            printf("%s = ", name);
            print_value(field);
            value_release(field);
        } else {
            printf("%s = ?", name);
        }
    }
}

static void cmd_sql(ReplSession* session, const char* query) {
    if (query == NULL || *query == '\0') {
        fprintf(stderr, "Usage: .sql <query>\n");
        return;
    }

    while (*query != '\0' && isspace((unsigned char)*query)) query++;

    if (session->driver_open) {
        if (strncasecmp(query, "SELECT ", 7) == 0) {
            void* result = NULL;
            if (!session->driver.query(&session->driver, query, NULL, 0, &result)) {
                printf("(empty result)\n");
                return;
            }
            int col_count = session->driver.result_column_count(&session->driver, result);
            void* row = NULL;
            int first = 1;
            while (session->driver.result_next(&session->driver, result, &row)) {
                if (!first) printf("\n");
                first = 0;
                print_driver_row(session, result, row, col_count);
            }
            if (first) {
                printf("(empty result)\n");
            } else {
                printf("\n");
            }
            session->driver.result_free(&session->driver, result);
            return;
        }
        if (!session->driver.exec(&session->driver, query, NULL, 0)) {
            fprintf(stderr, "SQL error: could not execute '%s'\n", query);
        }
        return;
    }

    if (strncasecmp(query, "SELECT ", 7) == 0) {
        Result* res = sql_exec(query, &session->ctx);
        if (res == NULL) {
            printf("(empty result)\n");
            return;
        }
        Row* row;
        int first = 1;
        while ((row = result_next(res)) != NULL) {
            if (!first) printf("\n");
            first = 0;
            for (int i = 0; i < row->field_count; i++) {
                if (i > 0) printf(" | ");
                printf("%s = ", row->fields[i].name);
                Cell c = row->fields[i].value;
                switch (c.type) {
                    case VAL_INT: printf("%d", c.as.as_int); break;
                    case VAL_FLOAT: printf("%g", c.as.as_float); break;
                    case VAL_STRING: printf("%s", c.as.as_string ? c.as.as_string : ""); break;
                    default: printf("?"); break;
                }
            }
        }
        if (first) {
            printf("(empty result)\n");
        } else {
            printf("\n");
        }
        result_free(res);
        return;
    }

    if (!sql_exec_ddl(query, &session->ctx)) {
        fprintf(stderr, "SQL error: could not execute '%s'\n", query);
    }
}

static void cmd_tables(ReplSession* session) {
#ifdef USE_SQLITE
    if (session->driver_open) {
        void* result = NULL;
        if (!session->driver.query(&session->driver,
                                   "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name",
                                   NULL, 0, &result)) {
            printf("(no tables)\n");
            return;
        }
        void* row = NULL;
        int first = 1;
        while (session->driver.result_next(&session->driver, result, &row)) {
            Value name;
            if (session->driver.row_get_field(&session->driver, row, "name", &name)) {
                print_value(name);
                printf("\n");
                value_release(name);
                first = 0;
            }
        }
        session->driver.result_free(&session->driver, result);
        if (first) printf("(no tables)\n");
        return;
    }
#endif

    int count = catalog_table_count(&session->ctx);
    if (count == 0) {
        printf("(no tables)\n");
        return;
    }
    for (int i = 0; i < count; i++) {
        const char* name = catalog_table_name(&session->ctx, i);
        printf("%s\n", name != NULL ? name : "?");
    }
}

static const char* type_name(int type) {
    switch (type) {
        case VAL_INT: return "int";
        case VAL_FLOAT: return "float";
        case VAL_STRING: return "string";
        default: return "?";
    }
}

#ifdef USE_SQLITE
static void print_driver_table_schema(ReplSession* session, const char* table_name) {
    char query[512];
    snprintf(query, sizeof(query), "PRAGMA table_info(%s)", table_name);
    void* result = NULL;
    if (!session->driver.query(&session->driver, query, NULL, 0, &result)) {
        return;
    }
    printf("%s(", table_name);
    void* row = NULL;
    int first_col = 1;
    while (session->driver.result_next(&session->driver, result, &row)) {
        Value name;
        Value type;
        int has_name = session->driver.row_get_field(&session->driver, row, "name", &name);
        int has_type = session->driver.row_get_field(&session->driver, row, "type", &type);
        if (has_name && name.type == VAL_STRING) {
            if (!first_col) printf(", ");
            first_col = 0;
            printf("%s", name.as.as_string ? name.as.as_string : "?");
            if (has_type && type.type == VAL_STRING && type.as.as_string != NULL) {
                printf(" %s", type.as.as_string);
            }
        }
        value_release(name);
        value_release(type);
    }
    printf(")\n");
    session->driver.result_free(&session->driver, result);
}
#endif

static int cmd_connect(ReplSession* session, const char* path) {
    if (path == NULL || *path == '\0') {
        fprintf(stderr, "Usage: .connect <path>\n");
        return 1;
    }
    while (*path != '\0' && isspace((unsigned char)*path)) path++;

#ifdef USE_SQLITE
    if (session->driver_open) {
        session->driver.close(&session->driver);
        session->driver_open = 0;
    } else {
        catalog_close(&session->ctx);
        session->ctx.db_path = DEFAULT_DB_PATH;
        session->ctx.pager = NULL;
    }

    sqlite_driver_init(&session->driver);
    if (!session->driver.open(&session->driver, path)) {
        fprintf(stderr, "Could not open database: %s\n", path);
        return 1;
    }
    session->driver_open = 1;
    if (session->vm != NULL) {
        vm_set_driver(session->vm, &session->driver);
    }
    printf("Connected to %s\n", path);
    return 0;
#else
    (void)session;
    fprintf(stderr, "SQLite support is disabled in this build\n");
    return 1;
#endif
}

static void cmd_schema(ReplSession* session, const char* table_name) {
#ifdef USE_SQLITE
    if (session->driver_open) {
        if (table_name != NULL) {
            print_driver_table_schema(session, table_name);
            return;
        }
        void* result = NULL;
        if (!session->driver.query(&session->driver,
                                   "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name",
                                   NULL, 0, &result)) {
            printf("(no tables)\n");
            return;
        }
        void* row = NULL;
        int first = 1;
        while (session->driver.result_next(&session->driver, result, &row)) {
            Value name;
            if (session->driver.row_get_field(&session->driver, row, "name", &name)) {
                if (name.type == VAL_STRING && name.as.as_string != NULL) {
                    print_driver_table_schema(session, name.as.as_string);
                    first = 0;
                }
                value_release(name);
            }
        }
        session->driver.result_free(&session->driver, result);
        if (first) printf("(no tables)\n");
        return;
    }
#endif

    int count = catalog_table_count(&session->ctx);
    if (count == 0) {
        printf("(no tables)\n");
        return;
    }

    for (int i = 0; i < count; i++) {
        const char* name = catalog_table_name(&session->ctx, i);
        if (table_name != NULL && strcmp(table_name, name) != 0) {
            continue;
        }
        printf("%s(", name);
        int cols = catalog_table_column_count(&session->ctx, i);
        for (int c = 0; c < cols; c++) {
            const char* col_name = catalog_table_column_name(&session->ctx, i, c);
            int col_type = catalog_table_column_type(&session->ctx, i, c);
            printf("%s %s", col_name != NULL ? col_name : "?", type_name(col_type));
            if (c < cols - 1) printf(", ");
        }
        printf(")\n");
        if (table_name != NULL) break;
    }
}

static void cmd_vars(ReplSession* session) {
    if (session->vm == NULL || session->var_count == 0) {
        printf("(no variables)\n");
        return;
    }
    int locals = vm_local_count(session->vm);
    for (int i = 0; i < session->var_count && i < locals; i++) {
        Value v = vm_local_get(session->vm, i);
        printf("%s = ", session->var_names[i] != NULL ? session->var_names[i] : "?");
        value_print(v);
        printf("\n");
    }
}

static void cmd_defs(ReplSession* session) {
    if (session->procedures.len == 0) {
        printf("(no procedures)\n");
        return;
    }
    printf("%s", session->procedures.data);
}

static void print_repl_help(void) {
    printf("MyPL REPL commands:\n");
    printf("  .exit             Quit the REPL\n");
    printf("  .quit             Same as .exit\n");
    printf("  .help             Show this help message\n");
    printf("  .load <file>      Load and run a MyPL source file\n");
    printf("  .tables           List all tables in the catalog\n");
    printf("  .schema [table]   Show schema for all tables or one table\n");
    printf("  .sql <query>      Execute a SQL DDL or SELECT query\n");
    printf("  .connect <path>   Connect to a SQLite database\n");
    printf("  .vars             Show current REPL variables\n");
    printf("  .defs             Show defined procedures\n");
}

void repl_run(const char* db_path) {
    ReplSession session;
    repl_session_init(&session, db_path);

    printf("MyPL REPL (type '.help' for commands, '.exit' to quit)\n");

    char line[LINE_SIZE];
    for (;;) {
        printf("> ");
        fflush(stdout);
        if (fgets(line, LINE_SIZE, stdin) == NULL) {
            printf("\n");
            break;
        }

        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
            len--;
        }

        trim_trailing_ws(line);
        if (len == 0) {
            continue;
        }

        if (line[0] == '.') {
            if (strcmp(line, ".exit") == 0 || strcmp(line, ".quit") == 0) {
                break;
            }
            if (strcmp(line, ".help") == 0) {
                print_repl_help();
                continue;
            }
            if (strcmp(line, ".tables") == 0) {
                cmd_tables(&session);
                continue;
            }
            if (strcmp(line, ".vars") == 0) {
                cmd_vars(&session);
                continue;
            }
            if (strcmp(line, ".defs") == 0) {
                cmd_defs(&session);
                continue;
            }
            if (strncmp(line, ".load ", 6) == 0) {
                cmd_load(&session, line + 6);
                continue;
            }
            if (strncmp(line, ".schema", 7) == 0) {
                const char* arg = line + 7;
                while (*arg != '\0' && isspace((unsigned char)*arg)) arg++;
                if (*arg == '\0') arg = NULL;
                cmd_schema(&session, arg);
                continue;
            }
            if (strncmp(line, ".sql ", 5) == 0) {
                cmd_sql(&session, line + 5);
                continue;
            }
            if (strncmp(line, ".connect ", 9) == 0) {
                cmd_connect(&session, line + 9);
                continue;
            }
            fprintf(stderr, "Unknown command: %s\n", line);
            continue;
        }

        if (is_procedure_definition(line)) {
            if (!string_buffer_append(&session.procedures, line) ||
                !string_buffer_append(&session.procedures, "\n")) {
                fprintf(stderr, "Out of memory\n");
                break;
            }
            /* Execute the procedure definition to validate it. */
            run_current_line(&session, "0");
        } else {
            if (is_statement(line)) {
                record_var_name(&session, line);
                if (!string_buffer_append(&session.main_body, line) ||
                    !string_buffer_append(&session.main_body, " ")) {
                    fprintf(stderr, "Out of memory\n");
                    break;
                }
            }
            run_current_line(&session, line);
        }
    }

    repl_session_free(&session);
}
