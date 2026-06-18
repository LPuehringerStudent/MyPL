#include <stdlib.h>
#include <string.h>

#include "sql_engine.h"

static const MockRow* g_mock_rows = NULL;
static int g_mock_row_count = 0;

void sql_engine_set_mock(const MockRow* rows, int row_count) {
    g_mock_rows = rows;
    g_mock_row_count = row_count;
}

Result* sql_exec(const char* query, Context* ctx) {
    (void)query;
    (void)ctx;

    Result* res = malloc(sizeof(Result));
    if (res == NULL) return NULL;

    res->row_count = g_mock_row_count;
    res->current = 0;
    if (g_mock_row_count > 0) {
        res->rows = malloc(sizeof(Row) * (size_t)g_mock_row_count);
        if (res->rows == NULL) {
            free(res);
            return NULL;
        }
        for (int i = 0; i < g_mock_row_count; i++) {
            res->rows[i].fields = g_mock_rows[i].fields;
            res->rows[i].field_count = g_mock_rows[i].field_count;
        }
    } else {
        res->rows = NULL;
    }
    return res;
}

Row* result_next(Result* res) {
    if (res == NULL || res->current >= res->row_count) {
        return NULL;
    }
    return &res->rows[res->current++];
}

void result_free(Result* res) {
    if (res == NULL) return;
    free(res->rows);
    free(res);
}

int row_get_field(Row* row, const char* name) {
    if (row == NULL || name == NULL) return 0;
    for (int i = 0; i < row->field_count; i++) {
        if (strcmp(row->fields[i].name, name) == 0) {
            return row->fields[i].value;
        }
    }
    return 0;
}
