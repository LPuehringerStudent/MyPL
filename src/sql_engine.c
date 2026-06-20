#define _GNU_SOURCE

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sql_engine.h"

/* -------------------------------------------------------------------------- */
/* Catalog                                                                    */
/* -------------------------------------------------------------------------- */

#define MAX_CATALOG_TABLES 64

static Table* g_catalog[MAX_CATALOG_TABLES];
static int    g_catalog_count = 0;

void catalog_clear(void) {
    for (int i = 0; i < g_catalog_count; i++) {
        Table* t = g_catalog[i];
        if (t == NULL) continue;

        for (int c = 0; c < t->column_count; c++) {
            free(t->columns[c].name);
        }
        free(t->columns);

        for (int r = 0; r < t->row_count; r++) {
            Row* row = &t->rows[r];
            for (int f = 0; f < row->field_count; f++) {
                free(row->fields[f].name);
                if (row->fields[f].value.type == VAL_STRING) {
                    free(row->fields[f].value.as.as_string);
                }
            }
            free(row->fields);
        }
        free(t->rows);

        free(t->name);
        free(t);
        g_catalog[i] = NULL;
    }
    g_catalog_count = 0;
}

Table* catalog_create_table(const char* name, const char** columns, int* types, int column_count) {
    if (g_catalog_count >= MAX_CATALOG_TABLES) return NULL;
    if (name == NULL || columns == NULL || types == NULL || column_count <= 0) return NULL;

    Table* t = calloc(1, sizeof(Table));
    if (t == NULL) return NULL;

    t->name = strdup(name);
    t->columns = calloc((size_t)column_count, sizeof(Column));
    if (t->columns == NULL) {
        free(t);
        return NULL;
    }
    t->column_count = column_count;

    for (int i = 0; i < column_count; i++) {
        t->columns[i].name = strdup(columns[i]);
        t->columns[i].type = types[i];
    }

    t->row_capacity = 8;
    t->rows = calloc((size_t)t->row_capacity, sizeof(Row));
    if (t->rows == NULL) {
        catalog_clear();
        return NULL;
    }

    g_catalog[g_catalog_count++] = t;
    return t;
}

Table* catalog_find_table(const char* name) {
    if (name == NULL) return NULL;
    for (int i = 0; i < g_catalog_count; i++) {
        if (g_catalog[i] != NULL && strcmp(g_catalog[i]->name, name) == 0) {
            return g_catalog[i];
        }
    }
    return NULL;
}

static char* strdup_or_null(const char* s) {
    if (s == NULL) return NULL;
    return strdup(s);
}

void catalog_insert(Table* table, Cell* cells) {
    if (table == NULL || cells == NULL) return;
    if (table->row_count >= table->row_capacity) {
        int new_capacity = table->row_capacity * 2;
        Row* new_rows = realloc(table->rows, (size_t)new_capacity * sizeof(Row));
        if (new_rows == NULL) return;
        table->rows = new_rows;
        table->row_capacity = new_capacity;
    }

    Row* row = &table->rows[table->row_count];
    row->field_count = table->column_count;
    row->fields = calloc((size_t)row->field_count, sizeof(Field));
    if (row->fields == NULL) return;

    for (int i = 0; i < row->field_count; i++) {
        row->fields[i].name = strdup_or_null(table->columns[i].name);
        row->fields[i].value.type = cells[i].type;
        if (cells[i].type == VAL_STRING) {
            row->fields[i].value.as.as_string = strdup_or_null(cells[i].as.as_string);
        } else if (cells[i].type == VAL_FLOAT) {
            row->fields[i].value.as.as_float = cells[i].as.as_float;
        } else {
            row->fields[i].value.as.as_int = cells[i].as.as_int;
        }
    }

    table->row_count++;
}

/* -------------------------------------------------------------------------- */
/* SQL parser                                                                 */
/* -------------------------------------------------------------------------- */

typedef enum {
    TOK_EOF,
    TOK_IDENT,
    TOK_NUMBER,
    TOK_STRING,
    TOK_STAR,
    TOK_COMMA,
    TOK_SELECT,
    TOK_FROM,
    TOK_WHERE,
    TOK_AND,
    TOK_EQ,
    TOK_LT,
    TOK_GT,
    TOK_LE,
    TOK_GE,
    TOK_NE
} SqlTokenType;

typedef struct {
    SqlTokenType type;
    const char*  text;
    int          length;
} SqlToken;

typedef struct {
    const char* start;
    const char* current;
} SqlLexer;

static void sql_lexer_init(SqlLexer* lex, const char* query) {
    lex->start = query;
    lex->current = query;
}

static void sql_skip_whitespace(SqlLexer* lex) {
    while (*lex->current != '\0' && isspace((unsigned char)*lex->current)) {
        lex->current++;
    }
}

static int sql_is_alpha_or_(char c) {
    return isalpha((unsigned char)c) || c == '_';
}

static SqlToken sql_make_token(SqlLexer* lex, SqlTokenType type) {
    SqlToken tok;
    tok.type = type;
    tok.text = lex->start;
    tok.length = (int)(lex->current - lex->start);
    return tok;
}

static SqlTokenType sql_check_keyword(const char* start, int length) {
    if (length == 6 && strncasecmp(start, "SELECT", 6) == 0) return TOK_SELECT;
    if (length == 4 && strncasecmp(start, "FROM", 4) == 0) return TOK_FROM;
    if (length == 5 && strncasecmp(start, "WHERE", 5) == 0) return TOK_WHERE;
    if (length == 3 && strncasecmp(start, "AND", 3) == 0) return TOK_AND;
    return TOK_IDENT;
}

static SqlToken sql_next_token(SqlLexer* lex) {
    sql_skip_whitespace(lex);
    lex->start = lex->current;

    if (*lex->current == '\0') {
        return sql_make_token(lex, TOK_EOF);
    }

    char c = *lex->current;

    if (sql_is_alpha_or_(c)) {
        while (sql_is_alpha_or_(*lex->current) || isdigit((unsigned char)*lex->current)) {
            lex->current++;
        }
        SqlTokenType type = sql_check_keyword(lex->start, (int)(lex->current - lex->start));
        return sql_make_token(lex, type);
    }

    if (isdigit((unsigned char)c)) {
        while (isdigit((unsigned char)*lex->current) || *lex->current == '.') {
            lex->current++;
        }
        return sql_make_token(lex, TOK_NUMBER);
    }

    if (c == '\'' || c == '"') {
        char quote = c;
        lex->current++;
        lex->start = lex->current;
        while (*lex->current != '\0' && *lex->current != quote) {
            lex->current++;
        }
        SqlToken tok = sql_make_token(lex, TOK_STRING);
        if (*lex->current == quote) {
            lex->current++;
        }
        return tok;
    }

    lex->current++;
    switch (c) {
        case '*': return sql_make_token(lex, TOK_STAR);
        case ',': return sql_make_token(lex, TOK_COMMA);
        case '=': return sql_make_token(lex, TOK_EQ);
        case '<':
            if (*lex->current == '=') { lex->current++; return sql_make_token(lex, TOK_LE); }
            if (*lex->current == '>') { lex->current++; return sql_make_token(lex, TOK_NE); }
            return sql_make_token(lex, TOK_LT);
        case '>':
            if (*lex->current == '=') { lex->current++; return sql_make_token(lex, TOK_GE); }
            return sql_make_token(lex, TOK_GT);
        default:
            return sql_make_token(lex, TOK_EOF);
    }
}

#define MAX_SELECT_COLUMNS 16

typedef struct {
    char column_names[MAX_SELECT_COLUMNS][64];
    int  column_count;
    char table_name[64];
    int  has_where;
    char where_column[64];
    int  where_op;
    Cell where_value;
} SelectStmt;

static void sql_token_text(SqlToken* tok, char* out, size_t out_size) {
    size_t len = (size_t)tok->length;
    if (len >= out_size) len = out_size - 1;
    memcpy(out, tok->text, len);
    out[len] = '\0';
}

static int sql_parse_select(const char* query, SelectStmt* stmt) {
    memset(stmt, 0, sizeof(*stmt));
    stmt->where_op = -1;

    SqlLexer lex;
    sql_lexer_init(&lex, query);

    SqlToken tok = sql_next_token(&lex);
    if (tok.type != TOK_SELECT) return 0;

    /* column list */
    tok = sql_next_token(&lex);
    if (tok.type == TOK_STAR) {
        stmt->column_count = 0;
        tok = sql_next_token(&lex);
    } else if (tok.type == TOK_IDENT) {
        sql_token_text(&tok, stmt->column_names[stmt->column_count++], sizeof(stmt->column_names[0]));
        while (1) {
            tok = sql_next_token(&lex);
            if (tok.type == TOK_COMMA) {
                tok = sql_next_token(&lex);
                if (tok.type != TOK_IDENT) return 0;
                if (stmt->column_count >= MAX_SELECT_COLUMNS) return 0;
                sql_token_text(&tok, stmt->column_names[stmt->column_count++], sizeof(stmt->column_names[0]));
            } else {
                break;
            }
        }
    } else {
        return 0;
    }

    if (tok.type != TOK_FROM) return 0;

    tok = sql_next_token(&lex);
    if (tok.type != TOK_IDENT) return 0;
    sql_token_text(&tok, stmt->table_name, sizeof(stmt->table_name));

    tok = sql_next_token(&lex);
    if (tok.type == TOK_EOF) {
        stmt->has_where = 0;
        return 1;
    }

    if (tok.type != TOK_WHERE) return 0;

    tok = sql_next_token(&lex);
    if (tok.type != TOK_IDENT) return 0;
    sql_token_text(&tok, stmt->where_column, sizeof(stmt->where_column));

    tok = sql_next_token(&lex);
    if (tok.type == TOK_EQ) stmt->where_op = 0;
    else if (tok.type == TOK_LT) stmt->where_op = 1;
    else if (tok.type == TOK_GT) stmt->where_op = 2;
    else if (tok.type == TOK_LE) stmt->where_op = 3;
    else if (tok.type == TOK_GE) stmt->where_op = 4;
    else if (tok.type == TOK_NE) stmt->where_op = 5;
    else return 0;

    tok = sql_next_token(&lex);
    if (tok.type == TOK_NUMBER) {
        char buf[64];
        sql_token_text(&tok, buf, sizeof(buf));
        if (strchr(buf, '.') != NULL) {
            stmt->where_value.type = VAL_FLOAT;
            stmt->where_value.as.as_float = strtod(buf, NULL);
        } else {
            stmt->where_value.type = VAL_INT;
            stmt->where_value.as.as_int = atoi(buf);
        }
    } else if (tok.type == TOK_STRING) {
        stmt->where_value.type = VAL_STRING;
        stmt->where_value.as.as_string = malloc((size_t)tok.length + 1);
        if (stmt->where_value.as.as_string == NULL) return 0;
        memcpy(stmt->where_value.as.as_string, tok.text, (size_t)tok.length);
        stmt->where_value.as.as_string[tok.length] = '\0';
    } else {
        return 0;
    }

    tok = sql_next_token(&lex);
    if (tok.type != TOK_EOF) return 0;

    stmt->has_where = 1;
    return 1;
}

static void sql_free_select_stmt(SelectStmt* stmt) {
    if (stmt->has_where && stmt->where_value.type == VAL_STRING) {
        free(stmt->where_value.as.as_string);
    }
}

/* -------------------------------------------------------------------------- */
/* Executor                                                                   */
/* -------------------------------------------------------------------------- */

static Cell cell_from_int(int v) {
    Cell c;
    c.type = VAL_INT;
    c.as.as_int = v;
    return c;
}

static int cell_compare(Cell* a, Cell* b) {
    if (a->type == VAL_INT && b->type == VAL_INT) {
        if (a->as.as_int < b->as.as_int) return -1;
        if (a->as.as_int > b->as.as_int) return 1;
        return 0;
    }
    if (a->type == VAL_FLOAT && b->type == VAL_FLOAT) {
        if (a->as.as_float < b->as.as_float) return -1;
        if (a->as.as_float > b->as.as_float) return 1;
        return 0;
    }
    if (a->type == VAL_STRING && b->type == VAL_STRING) {
        return strcmp(a->as.as_string, b->as.as_string);
    }
    if (a->type == VAL_FLOAT || b->type == VAL_FLOAT) {
        double av = (a->type == VAL_FLOAT) ? a->as.as_float : (double)a->as.as_int;
        double bv = (b->type == VAL_FLOAT) ? b->as.as_float : (double)b->as.as_int;
        if (av < bv) return -1;
        if (av > bv) return 1;
        return 0;
    }
    return 0;
}

static int evaluate_where(Row* row, SelectStmt* stmt) {
    Cell* value = NULL;
    for (int i = 0; i < row->field_count; i++) {
        if (strcmp(row->fields[i].name, stmt->where_column) == 0) {
            value = &row->fields[i].value;
            break;
        }
    }
    if (value == NULL) return 0;

    int cmp = cell_compare(value, &stmt->where_value);
    switch (stmt->where_op) {
        case 0: return cmp == 0;
        case 1: return cmp < 0;
        case 2: return cmp > 0;
        case 3: return cmp <= 0;
        case 4: return cmp >= 0;
        case 5: return cmp != 0;
        default: return 0;
    }
}

static Result* result_create(int capacity) {
    Result* res = calloc(1, sizeof(Result));
    if (res == NULL) return NULL;
    if (capacity > 0) {
        res->rows = calloc((size_t)capacity, sizeof(Row));
        if (res->rows == NULL) {
            free(res);
            return NULL;
        }
    }
    return res;
}

static void result_append(Result* res, Row* src, Table* table, SelectStmt* stmt) {
    Row* dst = &res->rows[res->row_count];
    res->row_count++;

    int count;
    if (stmt->column_count == 0) {
        count = table->column_count;
    } else {
        count = stmt->column_count;
    }

    dst->field_count = count;
    dst->fields = calloc((size_t)count, sizeof(Field));
    if (dst->fields == NULL) return;

    for (int i = 0; i < count; i++) {
        const char* name;
        Cell* value;
        if (stmt->column_count == 0) {
            name = table->columns[i].name;
            value = &src->fields[i].value;
        } else {
            name = stmt->column_names[i];
            value = NULL;
            for (int j = 0; j < src->field_count; j++) {
                if (strcmp(src->fields[j].name, name) == 0) {
                    value = &src->fields[j].value;
                    break;
                }
            }
            if (value == NULL) {
                /* Unknown column: leave as int 0 */
                dst->fields[i].name = strdup(name);
                dst->fields[i].value = cell_from_int(0);
                continue;
            }
        }

        dst->fields[i].name = strdup(name);
        dst->fields[i].value.type = value->type;
        if (value->type == VAL_STRING) {
            dst->fields[i].value.as.as_string = strdup(value->as.as_string);
        } else if (value->type == VAL_FLOAT) {
            dst->fields[i].value.as.as_float = value->as.as_float;
        } else {
            dst->fields[i].value.as.as_int = value->as.as_int;
        }
    }
}

static Result* execute_select(SelectStmt* stmt) {
    Table* table = catalog_find_table(stmt->table_name);
    if (table == NULL) {
        return result_create(0);
    }

    Result* res = result_create(table->row_count);
    if (res == NULL) return NULL;

    for (int i = 0; i < table->row_count; i++) {
        Row* row = &table->rows[i];
        if (stmt->has_where && !evaluate_where(row, stmt)) {
            continue;
        }
        result_append(res, row, table, stmt);
    }

    return res;
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

Result* sql_exec(const char* query, Context* ctx) {
    (void)ctx;

    SelectStmt stmt;
    if (!sql_parse_select(query, &stmt)) {
        return result_create(0);
    }

    Result* res = execute_select(&stmt);
    sql_free_select_stmt(&stmt);
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
    for (int i = 0; i < res->row_count; i++) {
        Row* row = &res->rows[i];
        for (int j = 0; j < row->field_count; j++) {
            free(row->fields[j].name);
            if (row->fields[j].value.type == VAL_STRING) {
                free(row->fields[j].value.as.as_string);
            }
        }
        free(row->fields);
    }
    free(res->rows);
    free(res);
}

Cell row_get_field(Row* row, const char* name) {
    Cell empty;
    empty.type = VAL_INT;
    empty.as.as_int = 0;
    if (row == NULL || name == NULL) return empty;
    for (int i = 0; i < row->field_count; i++) {
        if (row->fields[i].name != NULL && strcmp(row->fields[i].name, name) == 0) {
            return row->fields[i].value;
        }
    }
    return empty;
}
