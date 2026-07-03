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

static int sqlite_exec(DBDriver* driver, const char* sql, Value* params, int param_count) {
    SQLiteImpl* impl = (SQLiteImpl*)driver->impl;
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(impl->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(driver->error_message, sizeof(driver->error_message), "%s", sqlite3_errmsg(impl->db));
        return 0;
    }
    if (!bind_params(driver, stmt, params, param_count)) {
        sqlite3_finalize(stmt);
        return 0;
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        snprintf(driver->error_message, sizeof(driver->error_message), "%s", sqlite3_errmsg(impl->db));
    }
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE || rc == SQLITE_ROW) ? 1 : 0;
}

static int sqlite_query(DBDriver* driver, const char* sql, Value* params, int param_count, void** result_handle) {
    SQLiteImpl* impl = (SQLiteImpl*)driver->impl;
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(impl->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(driver->error_message, sizeof(driver->error_message), "%s", sqlite3_errmsg(impl->db));
        return 0;
    }
    if (!bind_params(driver, stmt, params, param_count)) {
        sqlite3_finalize(stmt);
        return 0;
    }
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
    return sqlite_exec(driver, "BEGIN", NULL, 0);
}

static int sqlite_commit(DBDriver* driver) {
    return sqlite_exec(driver, "COMMIT", NULL, 0);
}

static int sqlite_rollback(DBDriver* driver) {
    return sqlite_exec(driver, "ROLLBACK", NULL, 0);
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
