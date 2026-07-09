#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlite_driver.h"
#include "compiler.h"

typedef struct {
    sqlite3* db;
} SQLiteImpl;

static int sqlite_open(DBDriver* driver, const char* conn) {
    SQLiteImpl* impl = calloc(1, sizeof(SQLiteImpl));
    if (impl == NULL) {
        snprintf(driver->error_message, sizeof(driver->error_message), "out of memory");
        return 0;
    }
    int rc = sqlite3_open(conn, &impl->db);
    if (rc != SQLITE_OK) {
        snprintf(driver->error_message, sizeof(driver->error_message), "%s", sqlite3_errmsg(impl->db));
        sqlite3_close(impl->db);
        free(impl);
        return 0;
    }
    driver->impl = impl;
    driver->error_message[0] = '\0';
    return 1;
}

static void sqlite_close(DBDriver* driver) {
    SQLiteImpl* impl = (SQLiteImpl*)driver->impl;
    if (impl == NULL) return;
    sqlite3_close(impl->db);
    free(impl);
    driver->impl = NULL;
}

static int bind_params(DBDriver* driver, sqlite3_stmt* stmt, Value* params, int param_count) {
    for (int i = 0; i < param_count; i++) {
        int idx = i + 1;
        Value v = params[i];
        int rc = SQLITE_OK;
        switch (v.type) {
            case VAL_INT:
                rc = sqlite3_bind_int(stmt, idx, v.as.as_int);
                break;
            case VAL_FLOAT:
                rc = sqlite3_bind_double(stmt, idx, v.as.as_float);
                break;
            case VAL_STRING:
                rc = sqlite3_bind_text(stmt, idx,
                    v.as.as_string ? v.as.as_string : "", -1, SQLITE_TRANSIENT);
                break;
            case VAL_BOOL:
                rc = sqlite3_bind_int(stmt, idx, v.as.as_int);
                break;
            default:
                rc = sqlite3_bind_null(stmt, idx);
                break;
        }
        if (rc != SQLITE_OK) {
            snprintf(driver->error_message, sizeof(driver->error_message),
                     "parameter %d: %s", idx, sqlite3_errmsg((sqlite3*)stmt));
            return 0;
        }
    }
    return 1;
}

static int is_sql_alpha_(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_sql_alnum_(char c) {
    return is_sql_alpha_(c) || (c >= '0' && c <= '9');
}

static int sql_token_eq(const char* start, int len, const char* word) {
    int wlen = (int)strlen(word);
    if (len != wlen) return 0;
    for (int i = 0; i < len; i++) {
        char a = start[i];
        char b = word[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
    }
    return 1;
}

/*
 * Map MyPL type names to SQLite storage-class names so that string
 * columns keep TEXT affinity (instead of SQLite's default NUMERIC).
 * Quoted strings are left untouched so literal data is not altered.
 */
static char* sqlite_normalize_types(const char* sql) {
    size_t len = strlen(sql);
    char* out = malloc(len * 2 + 1);
    if (out == NULL) return NULL;
    size_t j = 0;
    const char* p = sql;
    while (*p != '\0') {
        if (*p == '\'' || *p == '"') {
            char quote = *p;
            out[j++] = *p++;
            while (*p != '\0' && *p != quote) {
                out[j++] = *p++;
            }
            if (*p == quote) {
                out[j++] = *p++;
            }
            continue;
        }
        if (is_sql_alpha_(*p)) {
            const char* start = p;
            while (is_sql_alnum_(*p)) p++;
            int tlen = (int)(p - start);
            const char* replacement = NULL;
            if (sql_token_eq(start, tlen, "string")) {
                replacement = "TEXT";
            } else if (sql_token_eq(start, tlen, "int")) {
                replacement = "INTEGER";
            } else if (sql_token_eq(start, tlen, "float")) {
                replacement = "REAL";
            } else if (sql_token_eq(start, tlen, "bool")) {
                replacement = "INTEGER";
            }
            if (replacement != NULL) {
                size_t rlen = strlen(replacement);
                memcpy(out + j, replacement, rlen);
                j += rlen;
            } else {
                memcpy(out + j, start, (size_t)tlen);
                j += (size_t)tlen;
            }
            continue;
        }
        out[j++] = *p++;
    }
    out[j] = '\0';
    return out;
}

static int sqlite_is_ddl(const char* sql) {
    const char* p = sql;
    while (*p != '\0' && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    size_t len = strlen(p);
    if (len >= 6 && strncasecmp(p, "CREATE", 6) == 0) return 1;
    if (len >= 4 && strncasecmp(p, "DROP", 4) == 0) return 1;
    if (len >= 5 && strncasecmp(p, "ALTER", 5) == 0) return 1;
    return 0;
}

static int sqlite_exec(DBDriver* driver, const char* sql, Value* params, int param_count) {
    SQLiteImpl* impl = (SQLiteImpl*)driver->impl;
    char* normalized = sqlite_normalize_types(sql);
    if (normalized == NULL) {
        snprintf(driver->error_message, sizeof(driver->error_message), "out of memory");
        return -1;
    }
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(impl->db, normalized, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(driver->error_message, sizeof(driver->error_message), "%s", sqlite3_errmsg(impl->db));
        free(normalized);
        return -1;
    }
    if (!bind_params(driver, stmt, params, param_count)) {
        sqlite3_finalize(stmt);
        free(normalized);
        return -1;
    }
    rc = sqlite3_step(stmt);
    int row_count = 0;
    if (rc == SQLITE_DONE || rc == SQLITE_ROW) {
        driver->error_message[0] = '\0';
        row_count = sqlite_is_ddl(normalized) ? 1 : sqlite3_changes(impl->db);
    } else {
        snprintf(driver->error_message, sizeof(driver->error_message), "%s", sqlite3_errmsg(impl->db));
        row_count = -1;
    }
    sqlite3_finalize(stmt);
    free(normalized);
    return row_count;
}

static int sqlite_query(DBDriver* driver, const char* sql, Value* params, int param_count, void** result_handle) {
    SQLiteImpl* impl = (SQLiteImpl*)driver->impl;
    char* normalized = sqlite_normalize_types(sql);
    if (normalized == NULL) {
        snprintf(driver->error_message, sizeof(driver->error_message), "out of memory");
        return 0;
    }
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(impl->db, normalized, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(driver->error_message, sizeof(driver->error_message), "%s", sqlite3_errmsg(impl->db));
        free(normalized);
        return 0;
    }
    if (!bind_params(driver, stmt, params, param_count)) {
        sqlite3_finalize(stmt);
        free(normalized);
        return 0;
    }
    free(normalized);
    *result_handle = stmt;
    driver->error_message[0] = '\0';
    return 1;
}

static int sqlite_result_next(DBDriver* driver, void* result_handle, void** row_handle) {
    (void)driver;
    sqlite3_stmt* stmt = (sqlite3_stmt*)result_handle;
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) return 0;
    *row_handle = stmt;
    return 1;
}

static int sqlite_row_get_field(DBDriver* driver, void* row_handle, const char* name, Value* out) {
    sqlite3_stmt* stmt = (sqlite3_stmt*)row_handle;
    int count = sqlite3_column_count(stmt);
    for (int i = 0; i < count; i++) {
        const char* col_name = sqlite3_column_name(stmt, i);
        if (col_name != NULL && strcmp(col_name, name) == 0) {
            driver->error_message[0] = '\0';
            int type = sqlite3_column_type(stmt, i);
            switch (type) {
                case SQLITE_INTEGER:
                    *out = value_int(sqlite3_column_int(stmt, i));
                    return 1;
                case SQLITE_FLOAT:
                    *out = value_float(sqlite3_column_double(stmt, i));
                    return 1;
                case SQLITE_TEXT:
                    *out = value_string(strdup((const char*)sqlite3_column_text(stmt, i)));
                    return 1;
                case SQLITE_NULL:
                    *out = value_int(0);
                    return 1;
                default:
                    *out = value_int(0);
                    return 1;
            }
        }
    }
    snprintf(driver->error_message, sizeof(driver->error_message),
             "column '%s' not found", name);
    *out = value_int(0);
    return 0;
}

static int sqlite_row_get_column(DBDriver* driver, void* row_handle, int index, Value* out) {
    sqlite3_stmt* stmt = (sqlite3_stmt*)row_handle;
    int count = sqlite3_column_count(stmt);
    if (index < 0 || index >= count) {
        snprintf(driver->error_message, sizeof(driver->error_message),
                 "column index %d out of range", index);
        *out = value_int(0);
        return 0;
    }
    driver->error_message[0] = '\0';
    int type = sqlite3_column_type(stmt, index);
    switch (type) {
        case SQLITE_INTEGER:
            *out = value_int(sqlite3_column_int(stmt, index));
            return 1;
        case SQLITE_FLOAT:
            *out = value_float(sqlite3_column_double(stmt, index));
            return 1;
        case SQLITE_TEXT:
            *out = value_string(strdup((const char*)sqlite3_column_text(stmt, index)));
            return 1;
        case SQLITE_NULL:
            *out = value_int(0);
            return 1;
        default:
            *out = value_int(0);
            return 1;
    }
}

static int sqlite_result_column_count(DBDriver* driver, void* result_handle) {
    (void)driver;
    sqlite3_stmt* stmt = (sqlite3_stmt*)result_handle;
    return sqlite3_column_count(stmt);
}

static const char* sqlite_result_column_name(DBDriver* driver, void* result_handle, int index) {
    (void)driver;
    sqlite3_stmt* stmt = (sqlite3_stmt*)result_handle;
    return sqlite3_column_name(stmt, index);
}

static void sqlite_result_free(DBDriver* driver, void* result_handle) {
    (void)driver;
    sqlite3_stmt* stmt = (sqlite3_stmt*)result_handle;
    sqlite3_finalize(stmt);
}

static int sqlite_begin(DBDriver* driver) {
    return sqlite_exec(driver, "BEGIN", NULL, 0) >= 0 ? 1 : 0;
}

static int sqlite_commit(DBDriver* driver) {
    return sqlite_exec(driver, "COMMIT", NULL, 0) >= 0 ? 1 : 0;
}

static int sqlite_rollback(DBDriver* driver) {
    return sqlite_exec(driver, "ROLLBACK", NULL, 0) >= 0 ? 1 : 0;
}

void sqlite_driver_init(DBDriver* driver) {
    driver->impl = NULL;
    driver->open = sqlite_open;
    driver->close = sqlite_close;
    driver->exec = sqlite_exec;
    driver->query = sqlite_query;
    driver->result_next = sqlite_result_next;
    driver->row_get_field = sqlite_row_get_field;
    driver->row_get_column = sqlite_row_get_column;
    driver->result_column_count = sqlite_result_column_count;
    driver->result_column_name = sqlite_result_column_name;
    driver->result_free = sqlite_result_free;
    driver->begin = sqlite_begin;
    driver->commit = sqlite_commit;
    driver->rollback = sqlite_rollback;
}
