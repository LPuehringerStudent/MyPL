#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "packages.h"
#include "os.h"

#ifdef USE_SQLITE
#include "sqlite_driver.h"
#endif

static char* strip_main_procedure(const char* source);

#define BUILTIN_DBMS_OUTPUT \
    "package dbms_output is\n" \
    "    proc enable(buffer_size int) -> int;\n" \
    "    proc put_line(line string) -> int;\n" \
    "    proc disable() -> int;\n" \
    "    func get_lines() -> array<string>;\n" \
    "end dbms_output;\n" \
    "\n" \
    "package body dbms_output is\n" \
    "    proc enable(buffer_size int) -> int {\n" \
    "        dbms_output_enable(buffer_size);\n" \
    "        return 0;\n" \
    "    }\n" \
    "\n" \
    "    proc put_line(line string) -> int {\n" \
    "        dbms_output_put_line(line);\n" \
    "        return 0;\n" \
    "    }\n" \
    "\n" \
    "    proc disable() -> int {\n" \
    "        dbms_output_disable();\n" \
    "        return 0;\n" \
    "    }\n" \
    "\n" \
    "    func get_lines() -> array<string> {\n" \
    "        return dbms_output_get_lines();\n" \
    "    }\n" \
    "end dbms_output;"

#define BUILTIN_UTL_FILE \
    "package utl_file is\n" \
    "    func fopen(path string, mode string) -> int;\n" \
    "    func get_line(handle int) -> string;\n" \
    "    proc put_line(handle int, text string) -> int;\n" \
    "    proc fclose(handle int) -> int;\n" \
    "end utl_file;\n" \
    "\n" \
    "package body utl_file is\n" \
    "    func fopen(path string, mode string) -> int {\n" \
    "        return utl_file_fopen(path, mode);\n" \
    "    }\n" \
    "\n" \
    "    func get_line(handle int) -> string {\n" \
    "        return utl_file_get_line(handle);\n" \
    "    }\n" \
    "\n" \
    "    proc put_line(handle int, text string) -> int {\n" \
    "        utl_file_put_line(handle, text);\n" \
    "        return 0;\n" \
    "    }\n" \
    "\n" \
    "    proc fclose(handle int) -> int {\n" \
    "        utl_file_fclose(handle);\n" \
    "        return 0;\n" \
    "    }\n" \
    "end utl_file;"

#define BUILTIN_DBMS_SQL \
    "package dbms_sql is\n" \
    "    proc execute(sql string) -> int;\n" \
    "    func query(sql string) -> array<row>;\n" \
    "end dbms_sql;\n" \
    "\n" \
    "package body dbms_sql is\n" \
    "    proc execute(sql string) -> int {\n" \
    "        return dbms_sql_execute(sql);\n" \
    "    }\n" \
    "\n" \
    "    func query(sql string) -> array<row> {\n" \
    "        return dbms_sql_query(sql);\n" \
    "    }\n" \
    "end dbms_sql;"

static const char* builtin_packages[] = {
    BUILTIN_DBMS_OUTPUT,
    BUILTIN_UTL_FILE,
    BUILTIN_DBMS_SQL,
    NULL
};

char* packages_load_builtins(void) {
    size_t total = 0;
    for (int i = 0; builtin_packages[i] != NULL; i++) {
        total += strlen(builtin_packages[i]) + 1;
    }
    if (total == 0) return NULL;
    char* result = malloc(total + 1);
    if (result == NULL) return NULL;
    result[0] = '\0';
    for (int i = 0; builtin_packages[i] != NULL; i++) {
        if (i > 0) strcat(result, "\n");
        strcat(result, builtin_packages[i]);
    }
    return result;
}

#ifdef USE_SQLITE
static const char* PACKAGES_TABLE =
    "CREATE TABLE IF NOT EXISTS _mypl_packages ("
    "    name TEXT PRIMARY KEY,"
    "    spec_source TEXT,"
    "    body_source TEXT"
    ")";

static int sqlite_save_source(DBDriver* driver, const char* source, int append) {
    sqlite3* db = ((SQLiteImpl*)driver->impl)->db;
    char* err = NULL;
    if (sqlite3_exec(db, PACKAGES_TABLE, NULL, NULL, &err) != SQLITE_OK) {
        if (err != NULL) {
            snprintf(driver->error_message, sizeof(driver->error_message), "%s", err);
            sqlite3_free(err);
        }
        return 0;
    }

    char* stripped = strip_main_procedure(source);
    if (stripped == NULL) {
        snprintf(driver->error_message, sizeof(driver->error_message), "out of memory");
        return 0;
    }

    sqlite3_stmt* stmt = NULL;
    const char* sql = append
        ? "INSERT INTO _mypl_packages (name, spec_source, body_source) "
          "VALUES ('__packages__', '', ?1) "
          "ON CONFLICT(name) DO UPDATE SET body_source = body_source || ?1"
        : "INSERT INTO _mypl_packages (name, spec_source, body_source) "
          "VALUES ('__packages__', '', ?1) "
          "ON CONFLICT(name) DO UPDATE SET body_source = ?1";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        snprintf(driver->error_message, sizeof(driver->error_message), "%s",
                 sqlite3_errmsg(db));
        free(stripped);
        return 0;
    }
    if (sqlite3_bind_text(stmt, 1, stripped, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        snprintf(driver->error_message, sizeof(driver->error_message), "%s",
                 sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        free(stripped);
        return 0;
    }
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    free(stripped);
    if (rc != SQLITE_DONE) {
        snprintf(driver->error_message, sizeof(driver->error_message), "%s",
                 sqlite3_errmsg(db));
        return 0;
    }
    return 1;
}

static char* sqlite_load_source(DBDriver* driver) {
    sqlite3* db = ((SQLiteImpl*)driver->impl)->db;
    sqlite3_stmt* check = NULL;
    if (sqlite3_prepare_v2(db,
                           "SELECT name FROM sqlite_master WHERE type='table' AND name='_mypl_packages'",
                           -1, &check, NULL) != SQLITE_OK) {
        return NULL;
    }
    int exists = sqlite3_step(check) == SQLITE_ROW;
    sqlite3_finalize(check);
    if (!exists) return NULL;

    sqlite3_stmt* stmt = NULL;
    const char* sql = "SELECT COALESCE(body_source, '') FROM _mypl_packages";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return NULL;
    }

    size_t total = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* text = (const char*)sqlite3_column_text(stmt, 0);
        if (text != NULL) total += strlen(text);
    }
    sqlite3_reset(stmt);

    if (total == 0) {
        sqlite3_finalize(stmt);
        return NULL;
    }

    char* result = malloc(total + 1);
    if (result == NULL) {
        sqlite3_finalize(stmt);
        return NULL;
    }
    result[0] = '\0';
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* text = (const char*)sqlite3_column_text(stmt, 0);
        if (text != NULL) strcat(result, text);
    }
    sqlite3_finalize(stmt);
    return result;
}
#endif

static char* sidecar_path(Context* ctx) {
    if (ctx == NULL || ctx->db_path == NULL) return NULL;
    size_t len = strlen(ctx->db_path) + strlen(".packages") + 1;
    char* path = malloc(len);
    if (path == NULL) return NULL;
    snprintf(path, len, "%s.packages", ctx->db_path);
    return path;
}

/* Remove top-level proc main blocks so persisted package sources can be
   prepended to later programs without creating a duplicate main. */
static char* strip_main_procedure(const char* source) {
    size_t len = strlen(source);
    char* out = malloc(len + 1);
    if (out == NULL) return NULL;
    size_t j = 0;
    const char* p = source;
    while (*p != '\0') {
        const char* line_start = p;
        while (*p != '\0' && (*p == ' ' || *p == '\t')) p++;
        if (strncmp(p, "proc main", 9) == 0) {
            /* Skip to matching brace, accounting for nested braces and strings. */
            int depth = 0;
            int in_string = 0;
            int escape = 0;
            int found_open = 0;
            while (*p != '\0') {
                if (escape) {
                    escape = 0;
                    p++;
                    continue;
                }
                if (*p == '\\' && in_string) {
                    escape = 1;
                    p++;
                    continue;
                }
                if (*p == '"') {
                    in_string = !in_string;
                    p++;
                    continue;
                }
                if (in_string) {
                    p++;
                    continue;
                }
                if (*p == '{') {
                    found_open = 1;
                    depth++;
                } else if (*p == '}') {
                    depth--;
                }
                p++;
                if (found_open && depth == 0) break;
            }
            /* Skip trailing whitespace/newline after the block. */
            while (*p != '\0' && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
        } else {
            while (line_start < p) out[j++] = *line_start++;
            while (*p != '\0' && *p != '\n') out[j++] = *p++;
            if (*p == '\n') out[j++] = *p++;
        }
    }
    out[j] = '\0';
    return out;
}

static int custom_save_source(Context* ctx, const char* source, int append) {
    char* path = sidecar_path(ctx);
    if (path == NULL) return 1;

    char* stripped = strip_main_procedure(source);
    if (stripped == NULL) {
        free(path);
        return 0;
    }

    FILE* f = fopen(path, append ? "a" : "w");
    if (f == NULL) {
        free(path);
        free(stripped);
        return 0;
    }
    fprintf(f, "// __MYPL_PACKAGE_SOURCE__\n%s\n", stripped);
    fclose(f);
    free(path);
    free(stripped);
    return 1;
}

static char* custom_load_source(Context* ctx) {
    char* path = sidecar_path(ctx);
    if (path == NULL) return NULL;
    char* data = os_read_file(path);
    free(path);
    return data;
}

char* packages_load_source(DBDriver* driver, Context* ctx) {
    if (driver == NULL) {
        /* No driver means the custom engine is being used directly via ctx. */
        return custom_load_source(ctx);
    }
    if (driver->is_sqlite) {
#ifdef USE_SQLITE
        return sqlite_load_source(driver);
#else
        (void)driver;
        return NULL;
#endif
    }
    return custom_load_source(ctx);
}

int packages_save_source(DBDriver* driver, Context* ctx, const char* source, int append) {
    if (source == NULL) return 1;
    if (driver == NULL) {
        /* No driver means the custom engine is being used directly via ctx. */
        return custom_save_source(ctx, source, append);
    }
    if (driver->is_sqlite) {
#ifdef USE_SQLITE
        return sqlite_save_source(driver, source, append);
#else
        (void)driver; (void)source; (void)append;
        return 1;
#endif
    }
    return custom_save_source(ctx, source, append);
}
