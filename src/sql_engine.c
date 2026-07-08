#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sql_engine.h"

/* -------------------------------------------------------------------------- */
/* In-memory catalog cache                                                    */
/* -------------------------------------------------------------------------- */

#define MAX_CATALOG_TABLES 64

static Table* g_catalog[MAX_CATALOG_TABLES];
static int    g_catalog_count = 0;
static int    g_catalog_page  = -1;

static void free_table(Table* t) {
    if (t == NULL) return;
    for (int i = 0; i < t->column_count; i++) {
        free(t->columns[i].name);
    }
    free(t->columns);
    free(t->name);
    free(t);
}

void catalog_clear(Context* ctx) {
    (void)ctx;
    for (int i = 0; i < g_catalog_count; i++) {
        free_table(g_catalog[i]);
        g_catalog[i] = NULL;
    }
    g_catalog_count = 0;
    g_catalog_page = -1;
}

/* -------------------------------------------------------------------------- */
/* Catalog serialization                                                      */
/* -------------------------------------------------------------------------- */

static int catalog_read_page(Context* ctx) {
    Pager* pager = ctx->pager;
    /* Page 1 is reserved for the catalog. */
    g_catalog_page = 1;

    uint8_t page[PAGE_SIZE];
    pager_read_page(pager, g_catalog_page, page);

    int offset = 0;
    uint32_t table_count = 0;
    memcpy(&table_count, page + offset, sizeof(table_count));
    offset += 4;

    for (uint32_t t = 0; t < table_count && g_catalog_count < MAX_CATALOG_TABLES; t++) {
        Table* table = calloc(1, sizeof(Table));
        if (table == NULL) return 0;

        uint8_t name_len = page[offset++];
        table->name = malloc((size_t)name_len + 1);
        if (table->name == NULL) {
            free(table);
            return 0;
        }
        memcpy(table->name, page + offset, name_len);
        table->name[name_len] = '\0';
        offset += name_len;

        uint8_t column_count = page[offset++];
        table->column_count = (int)column_count;
        table->columns = calloc((size_t)column_count, sizeof(Column));
        if (table->columns == NULL) {
            free_table(table);
            return 0;
        }

        for (int c = 0; c < (int)column_count; c++) {
            uint8_t col_name_len = page[offset++];
            table->columns[c].name = malloc((size_t)col_name_len + 1);
            if (table->columns[c].name == NULL) {
                free_table(table);
                return 0;
            }
            memcpy(table->columns[c].name, page + offset, col_name_len);
            table->columns[c].name[col_name_len] = '\0';
            offset += col_name_len;
            table->columns[c].type = page[offset++];
        }

        memcpy(&table->first_row_page, page + offset, sizeof(table->first_row_page));
        offset += 4;
        table->last_row_page = table->first_row_page;

        g_catalog[g_catalog_count++] = table;
    }

    return 1;
}

static int catalog_write_page(Context* ctx) {
    Pager* pager = ctx->pager;
    /* Page 1 is reserved for the catalog. */
    g_catalog_page = 1;

    uint8_t page[PAGE_SIZE];
    memset(page, 0, PAGE_SIZE);

    int offset = 0;
    uint32_t table_count = (uint32_t)g_catalog_count;
    memcpy(page + offset, &table_count, sizeof(table_count));
    offset += 4;

    for (int t = 0; t < g_catalog_count; t++) {
        Table* table = g_catalog[t];

        size_t name_len = strlen(table->name);
        if (name_len > MAX_NAME_LEN) name_len = MAX_NAME_LEN;
        page[offset++] = (uint8_t)name_len;
        memcpy(page + offset, table->name, name_len);
        offset += (int)name_len;

        page[offset++] = (uint8_t)table->column_count;
        for (int c = 0; c < table->column_count; c++) {
            size_t col_name_len = strlen(table->columns[c].name);
            if (col_name_len > MAX_NAME_LEN) col_name_len = MAX_NAME_LEN;
            page[offset++] = (uint8_t)col_name_len;
            memcpy(page + offset, table->columns[c].name, col_name_len);
            offset += (int)col_name_len;
            page[offset++] = (uint8_t)table->columns[c].type;
        }

        memcpy(page + offset, &table->first_row_page, sizeof(table->first_row_page));
        offset += 4;
    }

    pager_write_page(pager, g_catalog_page, page);
    return 1;
}

/* -------------------------------------------------------------------------- */
/* Catalog API                                                                */
/* -------------------------------------------------------------------------- */

int catalog_open(Context* ctx) {
    if (ctx == NULL || ctx->db_path == NULL) return 0;
    ctx->pager = pager_open(ctx->db_path);
    if (ctx->pager == NULL) return 0;
    catalog_clear(ctx);
    return catalog_read_page(ctx);
}

void catalog_close(Context* ctx) {
    if (ctx == NULL) return;
    if (ctx->pager != NULL) {
        catalog_write_page(ctx);
        pager_close(ctx->pager);
        ctx->pager = NULL;
    }
    catalog_clear(ctx);
}

Table* catalog_create_table(Context* ctx, const char* name, const char** columns, int* types, int column_count) {
    if (ctx == NULL || ctx->pager == NULL) return NULL;
    if (g_catalog_count >= MAX_CATALOG_TABLES) return NULL;
    if (name == NULL || columns == NULL || types == NULL || column_count <= 0) return NULL;
    if (catalog_find_table(ctx, name) != NULL) return NULL;

    Table* t = calloc(1, sizeof(Table));
    if (t == NULL) return NULL;

    t->name = strdup(name);
    t->columns = calloc((size_t)column_count, sizeof(Column));
    if (t->columns == NULL) {
        free_table(t);
        return NULL;
    }
    t->column_count = column_count;

    for (int i = 0; i < column_count; i++) {
        t->columns[i].name = strdup(columns[i]);
        t->columns[i].type = types[i];
    }

    t->first_row_page = 0;
    t->last_row_page = 0;

    g_catalog[g_catalog_count++] = t;
    catalog_write_page(ctx);
    return t;
}

Table* catalog_find_table(Context* ctx, const char* name) {
    (void)ctx;
    if (name == NULL) return NULL;
    for (int i = 0; i < g_catalog_count; i++) {
        if (g_catalog[i] != NULL && strcmp(g_catalog[i]->name, name) == 0) {
            return g_catalog[i];
        }
    }
    return NULL;
}

int catalog_table_count(Context* ctx) {
    (void)ctx;
    return g_catalog_count;
}

const char* catalog_table_name(Context* ctx, int index) {
    (void)ctx;
    if (index < 0 || index >= g_catalog_count) return NULL;
    return g_catalog[index]->name;
}

int catalog_table_column_count(Context* ctx, int index) {
    (void)ctx;
    if (index < 0 || index >= g_catalog_count) return 0;
    return g_catalog[index]->column_count;
}

const char* catalog_table_column_name(Context* ctx, int index, int col) {
    (void)ctx;
    if (index < 0 || index >= g_catalog_count) return NULL;
    if (col < 0 || col >= g_catalog[index]->column_count) return NULL;
    return g_catalog[index]->columns[col].name;
}

int catalog_table_column_type(Context* ctx, int index, int col) {
    (void)ctx;
    if (index < 0 || index >= g_catalog_count) return 0;
    if (col < 0 || col >= g_catalog[index]->column_count) return 0;
    return g_catalog[index]->columns[col].type;
}

/* -------------------------------------------------------------------------- */
/* Row serialization                                                          */
/* -------------------------------------------------------------------------- */

#define ROW_PAGE_HEADER_SIZE 8
#define ROW_PAGE_DATA_SIZE   (PAGE_SIZE - ROW_PAGE_HEADER_SIZE)

static int row_record_size(Table* table, Cell* cells) {
    int size = 2; /* record length prefix */
    for (int i = 0; i < table->column_count; i++) {
        size += 1; /* type tag */
        switch (cells[i].type) {
            case VAL_INT:    size += 4; break;
            case VAL_FLOAT:  size += 8; break;
            case VAL_STRING: size += 4 + (int)strlen(cells[i].as.as_string); break;
            default:         size += 4; break;
        }
    }
    return size;
}

static void serialize_row(Table* table, Cell* cells, uint8_t* out) {
    int offset = 2; /* length prefix */
    for (int i = 0; i < table->column_count; i++) {
        out[offset++] = (uint8_t)cells[i].type;
        switch (cells[i].type) {
            case VAL_INT: {
                int32_t v = (int32_t)cells[i].as.as_int;
                memcpy(out + offset, &v, sizeof(v));
                offset += sizeof(v);
                break;
            }
            case VAL_FLOAT: {
                double v = cells[i].as.as_float;
                memcpy(out + offset, &v, sizeof(v));
                offset += sizeof(v);
                break;
            }
            case VAL_STRING: {
                const char* s = cells[i].as.as_string ? cells[i].as.as_string : "";
                int32_t len = (int32_t)strlen(s);
                memcpy(out + offset, &len, sizeof(len));
                offset += sizeof(len);
                memcpy(out + offset, s, (size_t)len);
                offset += len;
                break;
            }
            default: {
                int32_t v = 0;
                memcpy(out + offset, &v, sizeof(v));
                offset += sizeof(v);
                break;
            }
        }
    }
    int16_t total = (int16_t)offset;
    memcpy(out, &total, sizeof(total));
}

static int deserialize_cell(const uint8_t* data, int* offset, Cell* cell) {
    cell->type = data[(*offset)++];
    switch (cell->type) {
        case VAL_INT: {
            int32_t v = 0;
            memcpy(&v, data + *offset, sizeof(v));
            *offset += sizeof(v);
            cell->as.as_int = (int)v;
            return 1;
        }
        case VAL_FLOAT: {
            double v = 0;
            memcpy(&v, data + *offset, sizeof(v));
            *offset += sizeof(v);
            cell->as.as_float = v;
            return 1;
        }
        case VAL_STRING: {
            int32_t len = 0;
            memcpy(&len, data + *offset, sizeof(len));
            *offset += sizeof(len);
            cell->as.as_string = malloc((size_t)len + 1);
            if (cell->as.as_string == NULL) return 0;
            memcpy(cell->as.as_string, data + *offset, (size_t)len);
            cell->as.as_string[len] = '\0';
            *offset += len;
            return 1;
        }
        default: {
            *offset += 4;
            cell->type = VAL_INT;
            cell->as.as_int = 0;
            return 1;
        }
    }
}

static Row* deserialize_row(Table* table, const uint8_t* record) {
    Row* row = malloc(sizeof(Row));
    if (row == NULL) return NULL;
    row->field_count = table->column_count;
    row->fields = calloc((size_t)row->field_count, sizeof(Field));
    if (row->fields == NULL) {
        free(row);
        return NULL;
    }

    int offset = 2; /* skip length prefix */
    for (int i = 0; i < row->field_count; i++) {
        row->fields[i].name = strdup(table->columns[i].name);
        if (!deserialize_cell(record, &offset, &row->fields[i].value)) {
            for (int j = 0; j <= i; j++) {
                free(row->fields[j].name);
                if (row->fields[j].value.type == VAL_STRING) {
                    free(row->fields[j].value.as.as_string);
                }
            }
            free(row->fields);
            free(row);
            return NULL;
        }
    }
    return row;
}

/* -------------------------------------------------------------------------- */
/* Row page storage                                                           */
/* -------------------------------------------------------------------------- */

static void row_page_init(uint8_t* page) {
    memset(page, 0, PAGE_SIZE);
    int16_t count = 0;
    int32_t free_ptr = ROW_PAGE_HEADER_SIZE;
    memcpy(page + 2, &count, sizeof(count));
    memcpy(page + 4, &free_ptr, sizeof(free_ptr));
}

static int row_page_append(Context* ctx, Table* table, Cell* cells) {
    Pager* pager = ctx->pager;
    int record_size = row_record_size(table, cells);
    if (record_size > ROW_PAGE_DATA_SIZE) return 0;

    uint8_t* record = calloc(1, (size_t)record_size);
    if (record == NULL) return 0;
    serialize_row(table, cells, record);

    uint8_t page[PAGE_SIZE];
    int page_num = table->last_row_page;

    if (page_num == 0) {
        /* First row for this table. */
        page_num = pager_allocate_page(pager);
        if (page_num < 0) {
            free(record);
            return 0;
        }
        row_page_init(page);
        table->first_row_page = page_num;
        table->last_row_page = page_num;
    } else {
        pager_read_page(pager, page_num, page);
    }

    int32_t free_ptr;
    memcpy(&free_ptr, page + 4, sizeof(free_ptr));
    int16_t count;
    memcpy(&count, page + 2, sizeof(count));

    if (free_ptr + record_size > PAGE_SIZE) {
        /* Page full: allocate next page. */
        int next_page_num = pager_allocate_page(pager);
        if (next_page_num < 0) {
            free(record);
            return 0;
        }
        memcpy(page, &next_page_num, sizeof(next_page_num));
        pager_write_page(pager, page_num, page);

        page_num = next_page_num;
        row_page_init(page);
        free_ptr = ROW_PAGE_HEADER_SIZE;
        count = 0;
        table->last_row_page = page_num;
    }

    memcpy(page + free_ptr, record, (size_t)record_size);
    free(record);
    free_ptr += record_size;
    count++;

    memcpy(page + 2, &count, sizeof(count));
    memcpy(page + 4, &free_ptr, sizeof(free_ptr));
    pager_write_page(pager, page_num, page);

    catalog_write_page(ctx);
    return 1;
}

void catalog_insert(Context* ctx, Table* table, Cell* cells) {
    if (ctx == NULL || ctx->pager == NULL || table == NULL || cells == NULL) return;
    row_page_append(ctx, table, cells);
}

/* -------------------------------------------------------------------------- */
/* Row scanning                                                               */
/* -------------------------------------------------------------------------- */

static int read_all_rows(Context* ctx, Table* table, Row** out_rows, int* out_count) {
    Pager* pager = ctx->pager;
    int capacity = 8;
    int count = 0;
    Row* rows = calloc((size_t)capacity, sizeof(Row));
    if (rows == NULL) return 0;

    int page_num = table->first_row_page;
    while (page_num != 0) {
        uint8_t page[PAGE_SIZE];
        pager_read_page(pager, page_num, page);

        int16_t record_count;
        memcpy(&record_count, page + 2, sizeof(record_count));

        int offset = ROW_PAGE_HEADER_SIZE;
        for (int r = 0; r < (int)record_count; r++) {
            int16_t record_size;
            memcpy(&record_size, page + offset, sizeof(record_size));

            Row* row = deserialize_row(table, page + offset);
            if (row == NULL) {
                for (int i = 0; i < count; i++) {
                    for (int j = 0; j < rows[i].field_count; j++) {
                        free(rows[i].fields[j].name);
                        if (rows[i].fields[j].value.type == VAL_STRING) {
                            free(rows[i].fields[j].value.as.as_string);
                        }
                    }
                    free(rows[i].fields);
                }
                free(rows);
                return 0;
            }

            if (count >= capacity) {
                capacity *= 2;
                Row* new_rows = realloc(rows, (size_t)capacity * sizeof(Row));
                if (new_rows == NULL) {
                    /* cleanup */
                    for (int j = 0; j < row->field_count; j++) {
                        free(row->fields[j].name);
                        if (row->fields[j].value.type == VAL_STRING) {
                            free(row->fields[j].value.as.as_string);
                        }
                    }
                    free(row->fields);
                    free(row);
                    for (int i = 0; i < count; i++) {
                        for (int j = 0; j < rows[i].field_count; j++) {
                            free(rows[i].fields[j].name);
                            if (rows[i].fields[j].value.type == VAL_STRING) {
                                free(rows[i].fields[j].value.as.as_string);
                            }
                        }
                        free(rows[i].fields);
                    }
                    free(rows);
                    return 0;
                }
                rows = new_rows;
            }

            rows[count++] = *row;
            free(row);
            offset += (int)record_size;
        }

        int32_t next_page;
        memcpy(&next_page, page, sizeof(next_page));
        page_num = (int)next_page;
    }

    *out_rows = rows;
    *out_count = count;
    return 1;
}

/* -------------------------------------------------------------------------- */
/* SQL parser (lexer + SELECT/CREATE/INSERT)                                  */
/* -------------------------------------------------------------------------- */

typedef enum {
    TOK_EOF,
    TOK_IDENT,
    TOK_NUMBER,
    TOK_STRING,
    TOK_STAR,
    TOK_COMMA,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_DOT,
    TOK_SELECT,
    TOK_FROM,
    TOK_WHERE,
    TOK_ORDER,
    TOK_GROUP,
    TOK_BY,
    TOK_ASC,
    TOK_DESC,
    TOK_LIMIT,
    TOK_JOIN,
    TOK_LEFT,
    TOK_OUTER,
    TOK_ON,
    TOK_COUNT,
    TOK_SUM,
    TOK_AVG,
    TOK_MIN,
    TOK_MAX,
    TOK_CREATE,
    TOK_TABLE,
    TOK_INSERT,
    TOK_INTO,
    TOK_VALUES,
    TOK_UPDATE,
    TOK_SET,
    TOK_DELETE,
    TOK_INT,
    TOK_FLOAT,
    TOK_STRING_KW,
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
    if (length == 5 && strncasecmp(start, "ORDER", 5) == 0) return TOK_ORDER;
    if (length == 5 && strncasecmp(start, "GROUP", 5) == 0) return TOK_GROUP;
    if (length == 2 && strncasecmp(start, "BY", 2) == 0) return TOK_BY;
    if (length == 3 && strncasecmp(start, "ASC", 3) == 0) return TOK_ASC;
    if (length == 4 && strncasecmp(start, "DESC", 4) == 0) return TOK_DESC;
    if (length == 5 && strncasecmp(start, "LIMIT", 5) == 0) return TOK_LIMIT;
    if (length == 4 && strncasecmp(start, "JOIN", 4) == 0) return TOK_JOIN;
    if (length == 4 && strncasecmp(start, "LEFT", 4) == 0) return TOK_LEFT;
    if (length == 5 && strncasecmp(start, "OUTER", 5) == 0) return TOK_OUTER;
    if (length == 2 && strncasecmp(start, "ON", 2) == 0) return TOK_ON;
    if (length == 5 && strncasecmp(start, "COUNT", 5) == 0) return TOK_COUNT;
    if (length == 3 && strncasecmp(start, "SUM", 3) == 0) return TOK_SUM;
    if (length == 3 && strncasecmp(start, "AVG", 3) == 0) return TOK_AVG;
    if (length == 3 && strncasecmp(start, "MIN", 3) == 0) return TOK_MIN;
    if (length == 3 && strncasecmp(start, "MAX", 3) == 0) return TOK_MAX;
    if (length == 6 && strncasecmp(start, "CREATE", 6) == 0) return TOK_CREATE;
    if (length == 5 && strncasecmp(start, "TABLE", 5) == 0) return TOK_TABLE;
    if (length == 6 && strncasecmp(start, "INSERT", 6) == 0) return TOK_INSERT;
    if (length == 4 && strncasecmp(start, "INTO", 4) == 0) return TOK_INTO;
    if (length == 6 && strncasecmp(start, "VALUES", 6) == 0) return TOK_VALUES;
    if (length == 6 && strncasecmp(start, "UPDATE", 6) == 0) return TOK_UPDATE;
    if (length == 3 && strncasecmp(start, "SET", 3) == 0) return TOK_SET;
    if (length == 6 && strncasecmp(start, "DELETE", 6) == 0) return TOK_DELETE;
    if (length == 3 && strncasecmp(start, "INT", 3) == 0) return TOK_INT;
    if (length == 5 && strncasecmp(start, "FLOAT", 5) == 0) return TOK_FLOAT;
    if (length == 6 && strncasecmp(start, "STRING", 6) == 0) return TOK_STRING_KW;
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
        case '(': return sql_make_token(lex, TOK_LPAREN);
        case ')': return sql_make_token(lex, TOK_RPAREN);
        case '.': return sql_make_token(lex, TOK_DOT);
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

static void sql_token_text(SqlToken* tok, char* out, size_t out_size) {
    size_t len = (size_t)tok->length;
    if (len >= out_size) len = out_size - 1;
    memcpy(out, tok->text, len);
    out[len] = '\0';
}

/* -------------------------------------------------------------------------- */
/* SELECT parsing & execution                                                 */
/* -------------------------------------------------------------------------- */

#define MAX_SELECT_COLUMNS 16
#define MAX_COLUMNS        16

typedef enum {
    AGG_NONE,
    AGG_COUNT,
    AGG_SUM,
    AGG_AVG,
    AGG_MIN,
    AGG_MAX
} AggregateFunc;

typedef struct {
    char column_names[MAX_SELECT_COLUMNS][MAX_NAME_LEN + 1];
    char column_table_prefix[MAX_SELECT_COLUMNS][MAX_NAME_LEN + 1];
    int  column_count;
    AggregateFunc column_aggregates[MAX_SELECT_COLUMNS];
    char column_aggregate_args[MAX_SELECT_COLUMNS][MAX_NAME_LEN + 1];
    int  column_aggregate_star[MAX_SELECT_COLUMNS];
    char table_name[MAX_NAME_LEN + 1];
    int  has_where;
    char where_table_prefix[MAX_NAME_LEN + 1];
    char where_column[MAX_NAME_LEN + 1];
    int  where_op;
    Cell where_value;
    int  has_join;
    int  join_type; /* 0 = inner, 1 = left */
    char join_table_name[MAX_NAME_LEN + 1];
    char join_left_column[MAX_NAME_LEN + 1];
    char join_right_column[MAX_NAME_LEN + 1];
    int  join_left_column_count;
    int  join_right_column_count;
    int  has_group_by;
    char group_by_table_prefix[MAX_NAME_LEN + 1];
    char group_by_column[MAX_NAME_LEN + 1];
    int  has_order_by;
    char order_by_table_prefix[MAX_NAME_LEN + 1];
    char order_by_column[MAX_NAME_LEN + 1];
    int  order_by_desc;
    int  has_limit;
    int  limit_count;
} SelectStmt;

static Cell* resolve_field(Row* row, const char* prefix, const char* name,
                           SelectStmt* stmt);

static int sql_parse_select(const char* query, SelectStmt* stmt) {
    memset(stmt, 0, sizeof(*stmt));
    stmt->where_op = -1;

    SqlLexer lex;
    sql_lexer_init(&lex, query);

    SqlToken tok = sql_next_token(&lex);
    if (tok.type != TOK_SELECT) return 0;

    tok = sql_next_token(&lex);
    if (tok.type == TOK_STAR) {
        stmt->column_count = 0;
        tok = sql_next_token(&lex);
    } else {
        while (1) {
            if (stmt->column_count >= MAX_SELECT_COLUMNS) return 0;

            AggregateFunc agg = AGG_NONE;
            if (tok.type == TOK_COUNT) agg = AGG_COUNT;
            else if (tok.type == TOK_SUM) agg = AGG_SUM;
            else if (tok.type == TOK_AVG) agg = AGG_AVG;
            else if (tok.type == TOK_MIN) agg = AGG_MIN;
            else if (tok.type == TOK_MAX) agg = AGG_MAX;

            if (agg != AGG_NONE) {
                stmt->column_aggregates[stmt->column_count] = agg;
                tok = sql_next_token(&lex);
                if (tok.type != TOK_LPAREN) return 0;
                tok = sql_next_token(&lex);
                if (tok.type == TOK_STAR) {
                    stmt->column_aggregate_star[stmt->column_count] = 1;
                    strcpy(stmt->column_aggregate_args[stmt->column_count], "*");
                    tok = sql_next_token(&lex);
                } else if (tok.type == TOK_IDENT) {
                    sql_token_text(&tok, stmt->column_aggregate_args[stmt->column_count],
                                   sizeof(stmt->column_aggregate_args[0]));
                    tok = sql_next_token(&lex);
                } else {
                    return 0;
                }
                if (tok.type != TOK_RPAREN) return 0;
                switch (agg) {
                    case AGG_COUNT: strcpy(stmt->column_names[stmt->column_count], "count"); break;
                    case AGG_SUM:   strcpy(stmt->column_names[stmt->column_count], "sum"); break;
                    case AGG_AVG:   strcpy(stmt->column_names[stmt->column_count], "avg"); break;
                    case AGG_MIN:   strcpy(stmt->column_names[stmt->column_count], "min"); break;
                    case AGG_MAX:   strcpy(stmt->column_names[stmt->column_count], "max"); break;
                    default: break;
                }
                stmt->column_count++;
                tok = sql_next_token(&lex);
            } else if (tok.type == TOK_IDENT) {
                char prefix_buf[MAX_NAME_LEN + 1] = "";
                char name_buf[MAX_NAME_LEN + 1];
                sql_token_text(&tok, name_buf, sizeof(name_buf));

                tok = sql_next_token(&lex);
                if (tok.type == TOK_DOT) {
                    SqlToken col_tok = sql_next_token(&lex);
                    if (col_tok.type != TOK_IDENT) return 0;
                    strcpy(prefix_buf, name_buf);
                    sql_token_text(&col_tok, name_buf, sizeof(name_buf));
                    tok = sql_next_token(&lex);
                }

                strcpy(stmt->column_names[stmt->column_count], name_buf);
                strcpy(stmt->column_table_prefix[stmt->column_count], prefix_buf);
                stmt->column_count++;
            } else {
                return 0;
            }

            if (tok.type == TOK_COMMA) {
                tok = sql_next_token(&lex);
            } else {
                break;
            }
        }
    }

    if (tok.type != TOK_FROM) return 0;

    tok = sql_next_token(&lex);
    if (tok.type != TOK_IDENT) return 0;
    sql_token_text(&tok, stmt->table_name, sizeof(stmt->table_name));

    tok = sql_next_token(&lex);
    if (tok.type == TOK_JOIN || tok.type == TOK_LEFT) {
        if (tok.type == TOK_LEFT) {
            stmt->join_type = 1;
            tok = sql_next_token(&lex);
            if (tok.type == TOK_OUTER) {
                tok = sql_next_token(&lex);
            }
            if (tok.type != TOK_JOIN) return 0;
            tok = sql_next_token(&lex);
        } else {
            stmt->join_type = 0;
            tok = sql_next_token(&lex);
        }
        if (tok.type != TOK_IDENT) return 0;
        sql_token_text(&tok, stmt->join_table_name, sizeof(stmt->join_table_name));
        stmt->has_join = 1;

        tok = sql_next_token(&lex);
        if (tok.type != TOK_ON) return 0;

        tok = sql_next_token(&lex);
        if (tok.type != TOK_IDENT) return 0;
        char left_table[MAX_NAME_LEN + 1];
        sql_token_text(&tok, left_table, sizeof(left_table));
        tok = sql_next_token(&lex);
        if (tok.type != TOK_DOT) return 0;
        tok = sql_next_token(&lex);
        if (tok.type != TOK_IDENT) return 0;
        sql_token_text(&tok, stmt->join_left_column, sizeof(stmt->join_left_column));

        tok = sql_next_token(&lex);
        if (tok.type != TOK_EQ) return 0;

        tok = sql_next_token(&lex);
        if (tok.type != TOK_IDENT) return 0;
        char right_table[MAX_NAME_LEN + 1];
        sql_token_text(&tok, right_table, sizeof(right_table));
        tok = sql_next_token(&lex);
        if (tok.type != TOK_DOT) return 0;
        tok = sql_next_token(&lex);
        if (tok.type != TOK_IDENT) return 0;
        sql_token_text(&tok, stmt->join_right_column, sizeof(stmt->join_right_column));

        if (strcmp(left_table, stmt->table_name) != 0 ||
            strcmp(right_table, stmt->join_table_name) != 0) {
            return 0;
        }

        tok = sql_next_token(&lex);
    }

    if (tok.type == TOK_WHERE) {
        tok = sql_next_token(&lex);
        if (tok.type != TOK_IDENT) return 0;
        char where_prefix_buf[MAX_NAME_LEN + 1] = "";
        char where_name_buf[MAX_NAME_LEN + 1];
        sql_token_text(&tok, where_name_buf, sizeof(where_name_buf));

        tok = sql_next_token(&lex);
        if (tok.type == TOK_DOT) {
            SqlToken col_tok = sql_next_token(&lex);
            if (col_tok.type != TOK_IDENT) return 0;
            strcpy(where_prefix_buf, where_name_buf);
            sql_token_text(&col_tok, where_name_buf, sizeof(where_name_buf));
            tok = sql_next_token(&lex);
        }
        strcpy(stmt->where_table_prefix, where_prefix_buf);
        strcpy(stmt->where_column, where_name_buf);

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
        stmt->has_where = 1;
        tok = sql_next_token(&lex);
    }

    if (tok.type == TOK_GROUP) {
        tok = sql_next_token(&lex);
        if (tok.type != TOK_BY) return 0;
        tok = sql_next_token(&lex);
        if (tok.type != TOK_IDENT) return 0;
        char group_prefix_buf[MAX_NAME_LEN + 1] = "";
        char group_name_buf[MAX_NAME_LEN + 1];
        sql_token_text(&tok, group_name_buf, sizeof(group_name_buf));

        tok = sql_next_token(&lex);
        if (tok.type == TOK_DOT) {
            SqlToken col_tok = sql_next_token(&lex);
            if (col_tok.type != TOK_IDENT) return 0;
            strcpy(group_prefix_buf, group_name_buf);
            sql_token_text(&col_tok, group_name_buf, sizeof(group_name_buf));
            tok = sql_next_token(&lex);
        }
        strcpy(stmt->group_by_table_prefix, group_prefix_buf);
        strcpy(stmt->group_by_column, group_name_buf);
        stmt->has_group_by = 1;
    }

    if (tok.type == TOK_ORDER) {
        tok = sql_next_token(&lex);
        if (tok.type != TOK_BY) return 0;
        tok = sql_next_token(&lex);
        if (tok.type != TOK_IDENT) return 0;
        char order_prefix_buf[MAX_NAME_LEN + 1] = "";
        char order_name_buf[MAX_NAME_LEN + 1];
        sql_token_text(&tok, order_name_buf, sizeof(order_name_buf));

        tok = sql_next_token(&lex);
        if (tok.type == TOK_DOT) {
            SqlToken col_tok = sql_next_token(&lex);
            if (col_tok.type != TOK_IDENT) return 0;
            strcpy(order_prefix_buf, order_name_buf);
            sql_token_text(&col_tok, order_name_buf, sizeof(order_name_buf));
            tok = sql_next_token(&lex);
        }
        strcpy(stmt->order_by_table_prefix, order_prefix_buf);
        strcpy(stmt->order_by_column, order_name_buf);
        stmt->has_order_by = 1;
        if (tok.type == TOK_ASC) {
            stmt->order_by_desc = 0;
            tok = sql_next_token(&lex);
        } else if (tok.type == TOK_DESC) {
            stmt->order_by_desc = 1;
            tok = sql_next_token(&lex);
        }
    }

    if (tok.type == TOK_LIMIT) {
        tok = sql_next_token(&lex);
        if (tok.type != TOK_NUMBER) return 0;
        char buf[64];
        sql_token_text(&tok, buf, sizeof(buf));
        stmt->limit_count = atoi(buf);
        if (stmt->limit_count < 0) stmt->limit_count = 0;
        stmt->has_limit = 1;
        tok = sql_next_token(&lex);
    }

    return tok.type == TOK_EOF;
}

static void sql_free_select_stmt(SelectStmt* stmt) {
    if (stmt->has_where && stmt->where_value.type == VAL_STRING) {
        free(stmt->where_value.as.as_string);
    }
}

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
    Cell* value = resolve_field(row, stmt->where_table_prefix, stmt->where_column, stmt);
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

static void free_rows(Row* rows, int count) {
    if (rows == NULL) return;
    for (int i = 0; i < count; i++) {
        for (int j = 0; j < rows[i].field_count; j++) {
            free(rows[i].fields[j].name);
            if (rows[i].fields[j].value.type == VAL_STRING) {
                free(rows[i].fields[j].value.as.as_string);
            }
        }
        free(rows[i].fields);
    }
    free(rows);
}

static Cell cell_dup(Cell* src) {
    Cell dst;
    dst.type = src->type;
    if (src->type == VAL_STRING) {
        dst.as.as_string = strdup(src->as.as_string != NULL ? src->as.as_string : "");
    } else if (src->type == VAL_FLOAT) {
        dst.as.as_float = src->as.as_float;
    } else {
        dst.as.as_int = src->as.as_int;
    }
    return dst;
}

static Row row_combine(Row* a, Row* b) {
    Row combined;
    combined.field_count = a->field_count + b->field_count;
    combined.fields = calloc((size_t)combined.field_count, sizeof(Field));
    if (combined.fields == NULL) {
        combined.field_count = 0;
        return combined;
    }

    int k = 0;
    for (int i = 0; i < a->field_count; i++) {
        combined.fields[k].name = strdup(a->fields[i].name);
        combined.fields[k].value = cell_dup(&a->fields[i].value);
        k++;
    }
    for (int i = 0; i < b->field_count; i++) {
        combined.fields[k].name = strdup(b->fields[i].name);
        combined.fields[k].value = cell_dup(&b->fields[i].value);
        k++;
    }

    return combined;
}

static Row row_zero(Table* table) {
    Row row;
    row.field_count = table->column_count;
    row.fields = calloc((size_t)row.field_count, sizeof(Field));
    if (row.fields == NULL) {
        row.field_count = 0;
        return row;
    }
    for (int i = 0; i < row.field_count; i++) {
        row.fields[i].name = strdup(table->columns[i].name);
        row.fields[i].value.type = table->columns[i].type;
        if (table->columns[i].type == VAL_STRING) {
            row.fields[i].value.as.as_string = strdup("");
        } else if (table->columns[i].type == VAL_FLOAT) {
            row.fields[i].value.as.as_float = 0.0;
        } else {
            row.fields[i].value.as.as_int = 0;
        }
    }
    return row;
}

static void free_row(Row* row) {
    if (row == NULL) return;
    for (int i = 0; i < row->field_count; i++) {
        free(row->fields[i].name);
        if (row->fields[i].value.type == VAL_STRING) {
            free(row->fields[i].value.as.as_string);
        }
    }
    free(row->fields);
}

static int evaluate_join(Row* left, Row* right, SelectStmt* stmt) {
    Cell* left_value = NULL;
    for (int i = 0; i < left->field_count; i++) {
        if (strcmp(left->fields[i].name, stmt->join_left_column) == 0) {
            left_value = &left->fields[i].value;
            break;
        }
    }
    if (left_value == NULL) return 0;

    Cell* right_value = NULL;
    for (int i = 0; i < right->field_count; i++) {
        if (strcmp(right->fields[i].name, stmt->join_right_column) == 0) {
            right_value = &right->fields[i].value;
            break;
        }
    }
    if (right_value == NULL) return 0;

    return cell_compare(left_value, right_value) == 0;
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

static void result_append(Result* res, Row* src, SelectStmt* stmt) {
    Row* dst = &res->rows[res->row_count];
    res->row_count++;

    int count;
    if (stmt->column_count == 0) {
        count = src->field_count;
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
            name = src->fields[i].name;
            value = &src->fields[i].value;
        } else {
            name = stmt->column_names[i];
            value = resolve_field(src, stmt->column_table_prefix[i], name, stmt);
            if (value == NULL) {
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

static Cell* row_find_field(Row* row, const char* name) {
    for (int i = 0; i < row->field_count; i++) {
        if (strcmp(row->fields[i].name, name) == 0) {
            return &row->fields[i].value;
        }
    }
    return NULL;
}

static Cell* resolve_field(Row* row, const char* prefix, const char* name,
                           SelectStmt* stmt) {
    if (prefix != NULL && prefix[0] != '\0') {
        int start = 0;
        int end = row->field_count;
        if (stmt->has_join) {
            if (strcmp(prefix, stmt->table_name) == 0) {
                end = stmt->join_left_column_count;
            } else if (strcmp(prefix, stmt->join_table_name) == 0) {
                start = stmt->join_left_column_count;
                end = stmt->join_left_column_count + stmt->join_right_column_count;
            } else {
                return NULL;
            }
        } else {
            if (strcmp(prefix, stmt->table_name) != 0) {
                return NULL;
            }
        }
        for (int i = start; i < end && i < row->field_count; i++) {
            if (strcmp(row->fields[i].name, name) == 0) {
                return &row->fields[i].value;
            }
        }
        return NULL;
    }
    return row_find_field(row, name);
}

static Cell zero_cell(void) {
    Cell c;
    c.type = VAL_INT;
    c.as.as_int = 0;
    return c;
}

/* Portable in-place insertion sort for Row* arrays. Avoids qsort_r which has
   incompatible signatures between GNU libc and BSD/macOS. */
static void row_ptr_sort(Row** rows, int count,
                         int (*compare)(Row* a, Row* b, void* arg),
                         void* arg) {
    for (int i = 1; i < count; i++) {
        Row* key = rows[i];
        int j = i - 1;
        while (j >= 0 && compare(rows[j], key, arg) > 0) {
            rows[j + 1] = rows[j];
            j--;
        }
        rows[j + 1] = key;
    }
}

static int filtered_row_compare(Row* a, Row* b, void* arg) {
    SelectStmt* stmt = (SelectStmt*)arg;
    Cell* ca = resolve_field(a, stmt->order_by_table_prefix, stmt->order_by_column, stmt);
    Cell* cb = resolve_field(b, stmt->order_by_table_prefix, stmt->order_by_column, stmt);
    Cell z = zero_cell();
    int cmp = cell_compare(ca != NULL ? ca : &z, cb != NULL ? cb : &z);
    return stmt->order_by_desc ? -cmp : cmp;
}

static int group_row_compare(Row* a, Row* b, void* arg) {
    SelectStmt* stmt = (SelectStmt*)arg;
    Cell* ca = resolve_field(a, stmt->group_by_table_prefix, stmt->group_by_column, stmt);
    Cell* cb = resolve_field(b, stmt->group_by_table_prefix, stmt->group_by_column, stmt);
    Cell z = zero_cell();
    return cell_compare(ca != NULL ? ca : &z, cb != NULL ? cb : &z);
}

static void result_limit(Result* res, int limit) {
    if (res == NULL || limit < 0) return;
    if (limit < res->row_count) {
        for (int i = limit; i < res->row_count; i++) {
            Row* row = &res->rows[i];
            for (int j = 0; j < row->field_count; j++) {
                free(row->fields[j].name);
                if (row->fields[j].value.type == VAL_STRING) {
                    free(row->fields[j].value.as.as_string);
                }
            }
            free(row->fields);
        }
        res->row_count = limit;
    }
}

static int stmt_has_aggregates(SelectStmt* stmt) {
    for (int i = 0; i < stmt->column_count; i++) {
        if (stmt->column_aggregates[i] != AGG_NONE) return 1;
    }
    return 0;
}

static Cell* source_row_find_field(Row* row, const char* name) {
    for (int i = 0; i < row->field_count; i++) {
        if (strcmp(row->fields[i].name, name) == 0) {
            return &row->fields[i].value;
        }
    }
    return NULL;
}

static Cell compute_aggregate(AggregateFunc agg, Row** rows, int row_count,
                              const char* arg) {
    Cell result;
    if (agg == AGG_COUNT) {
        result.type = VAL_INT;
        result.as.as_int = row_count;
        return result;
    }

    if (row_count == 0) {
        result.type = VAL_INT;
        result.as.as_int = 0;
        return result;
    }

    if (agg == AGG_SUM || agg == AGG_AVG) {
        double sum = 0;
        int numeric_count = 0;
        for (int i = 0; i < row_count; i++) {
            Cell* value = source_row_find_field(rows[i], arg);
            if (value == NULL) continue;
            if (value->type == VAL_INT) {
                sum += value->as.as_int;
                numeric_count++;
            } else if (value->type == VAL_FLOAT) {
                sum += value->as.as_float;
                numeric_count++;
            }
        }
        if (agg == AGG_AVG && numeric_count > 0) {
            result.type = VAL_FLOAT;
            result.as.as_float = sum / numeric_count;
        } else {
            result.type = VAL_FLOAT;
            result.as.as_float = sum;
        }
        return result;
    }

    if (agg == AGG_MIN || agg == AGG_MAX) {
        Cell* first = source_row_find_field(rows[0], arg);
        if (first == NULL) {
            result.type = VAL_INT;
            result.as.as_int = 0;
            return result;
        }
        result = *first;
        for (int i = 1; i < row_count; i++) {
            Cell* value = source_row_find_field(rows[i], arg);
            if (value == NULL) continue;
            int cmp = cell_compare(value, &result);
            if ((agg == AGG_MIN && cmp < 0) || (agg == AGG_MAX && cmp > 0)) {
                result = *value;
            }
        }
        return result;
    }

    result.type = VAL_INT;
    result.as.as_int = 0;
    return result;
}

static void result_append_aggregate(Result* res, SelectStmt* stmt,
                                    Row** rows, int row_count) {
    Row* dst = &res->rows[res->row_count++];
    dst->field_count = stmt->column_count;
    dst->fields = calloc((size_t)stmt->column_count, sizeof(Field));
    if (dst->fields == NULL) return;

    for (int i = 0; i < stmt->column_count; i++) {
        dst->fields[i].name = strdup(stmt->column_names[i]);
        if (stmt->column_aggregates[i] != AGG_NONE) {
            Cell agg_value = compute_aggregate(stmt->column_aggregates[i], rows,
                                               row_count,
                                               stmt->column_aggregate_args[i]);
            dst->fields[i].value = cell_dup(&agg_value);
        } else if (row_count > 0) {
            Cell* value = resolve_field(rows[0], stmt->column_table_prefix[i],
                                        stmt->column_names[i], stmt);
            if (value != NULL) {
                dst->fields[i].value = cell_dup(value);
            } else {
                dst->fields[i].value = cell_from_int(0);
            }
        } else {
            dst->fields[i].value = cell_from_int(0);
        }
    }
}

static Result* execute_select(Context* ctx, SelectStmt* stmt) {
    Row* source_rows = NULL;
    int source_count = 0;

    if (stmt->has_join) {
        Table* left_table = catalog_find_table(ctx, stmt->table_name);
        Table* right_table = catalog_find_table(ctx, stmt->join_table_name);
        if (left_table == NULL || right_table == NULL) {
            return result_create(0);
        }
        stmt->join_left_column_count = left_table->column_count;
        stmt->join_right_column_count = right_table->column_count;

        Row* left_rows = NULL;
        int left_count = 0;
        Row* right_rows = NULL;
        int right_count = 0;
        if (!read_all_rows(ctx, left_table, &left_rows, &left_count) ||
            !read_all_rows(ctx, right_table, &right_rows, &right_count)) {
            free_rows(left_rows, left_count);
            free_rows(right_rows, right_count);
            return result_create(0);
        }

        int max_combined = left_count * (right_count + 1);
        source_rows = calloc((size_t)max_combined, sizeof(Row));
        if (source_rows == NULL) {
            free_rows(left_rows, left_count);
            free_rows(right_rows, right_count);
            return NULL;
        }

        Row zero_right = row_zero(right_table);
        if (zero_right.fields == NULL) {
            free(source_rows);
            free_rows(left_rows, left_count);
            free_rows(right_rows, right_count);
            return NULL;
        }

        source_count = 0;
        for (int i = 0; i < left_count; i++) {
            int matched = 0;
            for (int j = 0; j < right_count; j++) {
                if (evaluate_join(&left_rows[i], &right_rows[j], stmt)) {
                    source_rows[source_count] = row_combine(&left_rows[i], &right_rows[j]);
                    if (source_rows[source_count].fields == NULL) {
                        free_row(&zero_right);
                        free_rows(source_rows, source_count);
                        free_rows(left_rows, left_count);
                        free_rows(right_rows, right_count);
                        return NULL;
                    }
                    source_count++;
                    matched = 1;
                }
            }
            if (!matched && stmt->join_type == 1) {
                source_rows[source_count] = row_combine(&left_rows[i], &zero_right);
                if (source_rows[source_count].fields == NULL) {
                    free_row(&zero_right);
                    free_rows(source_rows, source_count);
                    free_rows(left_rows, left_count);
                    free_rows(right_rows, right_count);
                    return NULL;
                }
                source_count++;
            }
        }

        free_row(&zero_right);
        free_rows(left_rows, left_count);
        free_rows(right_rows, right_count);
    } else {
        Table* table = catalog_find_table(ctx, stmt->table_name);
        if (table == NULL) {
            return result_create(0);
        }
        if (!read_all_rows(ctx, table, &source_rows, &source_count)) {
            return result_create(0);
        }
    }

    Row** filtered = malloc(sizeof(Row*) * (size_t)source_count);
    if (filtered == NULL) {
        free_rows(source_rows, source_count);
        return NULL;
    }
    int filtered_count = 0;
    for (int i = 0; i < source_count; i++) {
        if (stmt->has_where && !evaluate_where(&source_rows[i], stmt)) {
            continue;
        }
        filtered[filtered_count++] = &source_rows[i];
    }

    int has_agg = stmt_has_aggregates(stmt);

    if (stmt->has_order_by && !has_agg) {
        row_ptr_sort(filtered, filtered_count, filtered_row_compare, stmt);
    }

    Result* res = result_create(has_agg ? (stmt->has_group_by ? filtered_count : 1) : filtered_count);
    if (res == NULL) {
        free(filtered);
        free_rows(source_rows, source_count);
        return NULL;
    }

    if (has_agg) {
        if (stmt->has_group_by) {
            row_ptr_sort(filtered, filtered_count, group_row_compare, stmt);
            int i = 0;
            while (i < filtered_count) {
                int j = i + 1;
                Cell* key_i = resolve_field(filtered[i], stmt->group_by_table_prefix,
                                            stmt->group_by_column, stmt);
                Cell z = zero_cell();
                while (j < filtered_count) {
                    Cell* key_j = resolve_field(filtered[j], stmt->group_by_table_prefix,
                                                stmt->group_by_column, stmt);
                    if (cell_compare(key_i != NULL ? key_i : &z,
                                     key_j != NULL ? key_j : &z) != 0) {
                        break;
                    }
                    j++;
                }
                result_append_aggregate(res, stmt, &filtered[i], j - i);
                i = j;
            }
        } else {
            result_append_aggregate(res, stmt, filtered, filtered_count);
        }
    } else {
        for (int i = 0; i < filtered_count; i++) {
            result_append(res, filtered[i], stmt);
        }
    }

    free(filtered);
    free_rows(source_rows, source_count);

    if (stmt->has_limit && !has_agg) {
        result_limit(res, stmt->limit_count);
    }

    return res;
}

/* -------------------------------------------------------------------------- */
/* CREATE TABLE / INSERT parsing and execution                                */
/* -------------------------------------------------------------------------- */

typedef struct {
    char        table_name[MAX_NAME_LEN + 1];
    char*       column_names[MAX_COLUMNS];
    int         column_types[MAX_COLUMNS];
    int         column_count;
    Cell        values[MAX_COLUMNS];
    int         value_count;
    int         has_select;
    char*       select_query;
} DdlStmt;

static int sql_parse_type(SqlToken* tok, int* out_type) {
    if (tok->type == TOK_INT) {
        *out_type = VAL_INT;
        return 1;
    }
    if (tok->type == TOK_FLOAT) {
        *out_type = VAL_FLOAT;
        return 1;
    }
    if (tok->type == TOK_STRING_KW) {
        *out_type = VAL_STRING;
        return 1;
    }
    return 0;
}

static int sql_parse_create_table(const char* query, DdlStmt* stmt) {
    memset(stmt, 0, sizeof(*stmt));

    SqlLexer lex;
    sql_lexer_init(&lex, query);

    SqlToken tok = sql_next_token(&lex);
    if (tok.type != TOK_CREATE) return 0;
    tok = sql_next_token(&lex);
    if (tok.type != TOK_TABLE) return 0;
    tok = sql_next_token(&lex);
    if (tok.type != TOK_IDENT) return 0;
    sql_token_text(&tok, stmt->table_name, sizeof(stmt->table_name));

    tok = sql_next_token(&lex);
    if (tok.type != TOK_LPAREN) return 0;

    tok = sql_next_token(&lex);
    if (tok.type != TOK_IDENT) return 0;
    char buf[MAX_NAME_LEN + 1];
    sql_token_text(&tok, buf, sizeof(buf));
    stmt->column_names[stmt->column_count] = strdup(buf);
    if (stmt->column_names[stmt->column_count] == NULL) return 0;
    tok = sql_next_token(&lex);
    if (!sql_parse_type(&tok, &stmt->column_types[stmt->column_count])) return 0;
    stmt->column_count++;

    while (1) {
        tok = sql_next_token(&lex);
        if (tok.type == TOK_RPAREN) break;
        if (tok.type != TOK_COMMA) return 0;
        tok = sql_next_token(&lex);
        if (tok.type != TOK_IDENT) return 0;
        sql_token_text(&tok, buf, sizeof(buf));
        stmt->column_names[stmt->column_count] = strdup(buf);
        if (stmt->column_names[stmt->column_count] == NULL) return 0;
        tok = sql_next_token(&lex);
        if (!sql_parse_type(&tok, &stmt->column_types[stmt->column_count])) return 0;
        stmt->column_count++;
    }

    tok = sql_next_token(&lex);
    return tok.type == TOK_EOF;
}

static int sql_parse_insert(const char* query, DdlStmt* stmt) {
    memset(stmt, 0, sizeof(*stmt));

    SqlLexer lex;
    sql_lexer_init(&lex, query);

    SqlToken tok = sql_next_token(&lex);
    if (tok.type != TOK_INSERT) return 0;
    tok = sql_next_token(&lex);
    if (tok.type != TOK_INTO) return 0;
    tok = sql_next_token(&lex);
    if (tok.type != TOK_IDENT) return 0;
    sql_token_text(&tok, stmt->table_name, sizeof(stmt->table_name));

    tok = sql_next_token(&lex);
    if (tok.type == TOK_SELECT) {
        stmt->has_select = 1;
        stmt->select_query = strdup(tok.text);
        return stmt->select_query != NULL;
    }
    if (tok.type != TOK_VALUES) return 0;
    tok = sql_next_token(&lex);
    if (tok.type != TOK_LPAREN) return 0;

    tok = sql_next_token(&lex);
    while (tok.type != TOK_RPAREN && tok.type != TOK_EOF) {
        if (stmt->value_count >= MAX_COLUMNS) return 0;
        if (tok.type == TOK_NUMBER) {
            char buf[64];
            sql_token_text(&tok, buf, sizeof(buf));
            if (strchr(buf, '.') != NULL) {
                stmt->values[stmt->value_count].type = VAL_FLOAT;
                stmt->values[stmt->value_count].as.as_float = strtod(buf, NULL);
            } else {
                stmt->values[stmt->value_count].type = VAL_INT;
                stmt->values[stmt->value_count].as.as_int = atoi(buf);
            }
        } else if (tok.type == TOK_STRING) {
            stmt->values[stmt->value_count].type = VAL_STRING;
            stmt->values[stmt->value_count].as.as_string = malloc((size_t)tok.length + 1);
            if (stmt->values[stmt->value_count].as.as_string == NULL) return 0;
            memcpy(stmt->values[stmt->value_count].as.as_string, tok.text, (size_t)tok.length);
            stmt->values[stmt->value_count].as.as_string[tok.length] = '\0';
        } else {
            return 0;
        }
        stmt->value_count++;

        tok = sql_next_token(&lex);
        if (tok.type == TOK_COMMA) {
            tok = sql_next_token(&lex);
        }
    }

    if (tok.type != TOK_RPAREN) return 0;
    tok = sql_next_token(&lex);
    return tok.type == TOK_EOF;
}

static void sql_free_ddl_stmt(DdlStmt* stmt) {
    for (int i = 0; i < stmt->column_count; i++) {
        free(stmt->column_names[i]);
        stmt->column_names[i] = NULL;
    }
    for (int i = 0; i < stmt->value_count; i++) {
        if (stmt->values[i].type == VAL_STRING) {
            free(stmt->values[i].as.as_string);
        }
    }
    if (stmt->select_query != NULL) {
        free(stmt->select_query);
        stmt->select_query = NULL;
    }
}

typedef struct {
    char table_name[MAX_NAME_LEN + 1];
    char set_column[MAX_NAME_LEN + 1];
    Cell set_value;
    int has_where;
    char where_column[MAX_NAME_LEN + 1];
    int where_op;
    Cell where_value;
} UpdateStmt;

typedef struct {
    char table_name[MAX_NAME_LEN + 1];
    int has_where;
    char where_column[MAX_NAME_LEN + 1];
    int where_op;
    Cell where_value;
} DeleteStmt;

static int sql_parse_update(const char* query, UpdateStmt* stmt);
static int sql_parse_delete(const char* query, DeleteStmt* stmt);
static void sql_free_update_stmt(UpdateStmt* stmt);
static void sql_free_delete_stmt(DeleteStmt* stmt);
static int execute_update(Context* ctx, UpdateStmt* stmt);
static int execute_delete(Context* ctx, DeleteStmt* stmt);

static int execute_insert_select(Context* ctx, Table* table, const char* select_query) {
    Result* res = sql_exec(select_query, ctx);
    if (res == NULL) return 0;

    for (int r = 0; r < res->row_count; r++) {
        Row* src = &res->rows[r];
        Cell cells[MAX_COLUMNS];
        for (int c = 0; c < table->column_count && c < MAX_COLUMNS; c++) {
            Cell* value = row_find_field(src, table->columns[c].name);
            if (value != NULL) {
                cells[c] = cell_dup(value);
            } else {
                cells[c].type = table->columns[c].type;
                if (cells[c].type == VAL_STRING) {
                    cells[c].as.as_string = strdup("");
                } else if (cells[c].type == VAL_FLOAT) {
                    cells[c].as.as_float = 0.0;
                } else {
                    cells[c].as.as_int = 0;
                }
            }
        }
        catalog_insert(ctx, table, cells);
        for (int c = 0; c < table->column_count && c < MAX_COLUMNS; c++) {
            if (cells[c].type == VAL_STRING) {
                free(cells[c].as.as_string);
            }
        }
    }

    result_free(res);
    catalog_write_page(ctx);
    return 1;
}

int sql_exec_ddl(const char* query, Context* ctx) {
    DdlStmt stmt;
    if (sql_parse_create_table(query, &stmt)) {
        Table* t = catalog_create_table(ctx, stmt.table_name,
                                        (const char**)stmt.column_names,
                                        stmt.column_types,
                                        stmt.column_count);
        return t != NULL ? 1 : 0;
    }

    if (sql_parse_insert(query, &stmt)) {
        Table* t = catalog_find_table(ctx, stmt.table_name);
        if (t == NULL) {
            sql_free_ddl_stmt(&stmt);
            return 0;
        }
        if (stmt.has_select) {
            int ok = execute_insert_select(ctx, t, stmt.select_query);
            sql_free_ddl_stmt(&stmt);
            return ok;
        }
        if (t->column_count != stmt.value_count) {
            sql_free_ddl_stmt(&stmt);
            return 0;
        }
        catalog_insert(ctx, t, stmt.values);
        sql_free_ddl_stmt(&stmt);
        return 1;
    }

    UpdateStmt update_stmt;
    if (sql_parse_update(query, &update_stmt)) {
        int ok = execute_update(ctx, &update_stmt);
        sql_free_update_stmt(&update_stmt);
        return ok;
    }

    DeleteStmt delete_stmt;
    if (sql_parse_delete(query, &delete_stmt)) {
        int ok = execute_delete(ctx, &delete_stmt);
        sql_free_delete_stmt(&delete_stmt);
        return ok;
    }

    return 0;
}

/* -------------------------------------------------------------------------- */
/* UPDATE / DELETE parsing and execution                                      */
/* -------------------------------------------------------------------------- */

static int parse_where_clause(SqlLexer* lex, int* has_where,
                               char* where_column, size_t where_column_size,
                               int* where_op, Cell* where_value) {
    *has_where = 0;
    SqlToken tok = sql_next_token(lex);
    if (tok.type == TOK_EOF) {
        return 1;
    }
    if (tok.type != TOK_WHERE) return 0;

    tok = sql_next_token(lex);
    if (tok.type != TOK_IDENT) return 0;
    sql_token_text(&tok, where_column, (int)where_column_size);

    tok = sql_next_token(lex);
    if (tok.type == TOK_EQ) *where_op = 0;
    else if (tok.type == TOK_LT) *where_op = 1;
    else if (tok.type == TOK_GT) *where_op = 2;
    else if (tok.type == TOK_LE) *where_op = 3;
    else if (tok.type == TOK_GE) *where_op = 4;
    else if (tok.type == TOK_NE) *where_op = 5;
    else return 0;

    tok = sql_next_token(lex);
    if (tok.type == TOK_NUMBER) {
        char buf[64];
        sql_token_text(&tok, buf, sizeof(buf));
        if (strchr(buf, '.') != NULL) {
            where_value->type = VAL_FLOAT;
            where_value->as.as_float = strtod(buf, NULL);
        } else {
            where_value->type = VAL_INT;
            where_value->as.as_int = atoi(buf);
        }
    } else if (tok.type == TOK_STRING) {
        where_value->type = VAL_STRING;
        where_value->as.as_string = malloc((size_t)tok.length + 1);
        if (where_value->as.as_string == NULL) return 0;
        memcpy(where_value->as.as_string, tok.text, (size_t)tok.length);
        where_value->as.as_string[tok.length] = '\0';
    } else {
        return 0;
    }

    tok = sql_next_token(lex);
    if (tok.type != TOK_EOF) return 0;

    *has_where = 1;
    return 1;
}

static void sql_free_update_stmt(UpdateStmt* stmt) {
    if (stmt->where_value.type == VAL_STRING) {
        free(stmt->where_value.as.as_string);
        stmt->where_value.as.as_string = NULL;
    }
}

static void sql_free_delete_stmt(DeleteStmt* stmt) {
    if (stmt->where_value.type == VAL_STRING) {
        free(stmt->where_value.as.as_string);
        stmt->where_value.as.as_string = NULL;
    }
}

static int sql_parse_update(const char* query, UpdateStmt* stmt) {
    memset(stmt, 0, sizeof(*stmt));

    SqlLexer lex;
    sql_lexer_init(&lex, query);

    SqlToken tok = sql_next_token(&lex);
    if (tok.type != TOK_UPDATE) return 0;
    tok = sql_next_token(&lex);
    if (tok.type != TOK_IDENT) return 0;
    sql_token_text(&tok, stmt->table_name, sizeof(stmt->table_name));

    tok = sql_next_token(&lex);
    if (tok.type != TOK_SET) return 0;

    tok = sql_next_token(&lex);
    if (tok.type != TOK_IDENT) return 0;
    sql_token_text(&tok, stmt->set_column, sizeof(stmt->set_column));

    tok = sql_next_token(&lex);
    if (tok.type != TOK_EQ) return 0;

    tok = sql_next_token(&lex);
    if (tok.type == TOK_NUMBER) {
        char buf[64];
        sql_token_text(&tok, buf, sizeof(buf));
        if (strchr(buf, '.') != NULL) {
            stmt->set_value.type = VAL_FLOAT;
            stmt->set_value.as.as_float = strtod(buf, NULL);
        } else {
            stmt->set_value.type = VAL_INT;
            stmt->set_value.as.as_int = atoi(buf);
        }
    } else if (tok.type == TOK_STRING) {
        stmt->set_value.type = VAL_STRING;
        stmt->set_value.as.as_string = malloc((size_t)tok.length + 1);
        if (stmt->set_value.as.as_string == NULL) return 0;
        memcpy(stmt->set_value.as.as_string, tok.text, (size_t)tok.length);
        stmt->set_value.as.as_string[tok.length] = '\0';
    } else {
        return 0;
    }

    return parse_where_clause(&lex, &stmt->has_where,
                              stmt->where_column, sizeof(stmt->where_column),
                              &stmt->where_op, &stmt->where_value);
}

static int sql_parse_delete(const char* query, DeleteStmt* stmt) {
    memset(stmt, 0, sizeof(*stmt));

    SqlLexer lex;
    sql_lexer_init(&lex, query);

    SqlToken tok = sql_next_token(&lex);
    if (tok.type != TOK_DELETE) return 0;
    tok = sql_next_token(&lex);
    if (tok.type != TOK_FROM) return 0;
    tok = sql_next_token(&lex);
    if (tok.type != TOK_IDENT) return 0;
    sql_token_text(&tok, stmt->table_name, sizeof(stmt->table_name));

    return parse_where_clause(&lex, &stmt->has_where,
                              stmt->where_column, sizeof(stmt->where_column),
                              &stmt->where_op, &stmt->where_value);
}

static int row_matches_where(Row* row, const char* where_column, int where_op, Cell* where_value) {
    Cell* value = NULL;
    for (int i = 0; i < row->field_count; i++) {
        if (strcmp(row->fields[i].name, where_column) == 0) {
            value = &row->fields[i].value;
            break;
        }
    }
    if (value == NULL) return 0;

    int cmp = cell_compare(value, where_value);
    switch (where_op) {
        case 0: return cmp == 0;
        case 1: return cmp < 0;
        case 2: return cmp > 0;
        case 3: return cmp <= 0;
        case 4: return cmp >= 0;
        case 5: return cmp != 0;
        default: return 0;
    }
}

static void free_row_pages(Context* ctx, Table* table) {
    Pager* pager = ctx->pager;
    int page_num = table->first_row_page;
    while (page_num != 0) {
        uint8_t page[PAGE_SIZE];
        pager_read_page(pager, page_num, page);
        int32_t next_page_num;
        memcpy(&next_page_num, page, sizeof(next_page_num));
        pager_free_page(pager, page_num);
        page_num = next_page_num;
    }
    table->first_row_page = 0;
    table->last_row_page = 0;
}

static int execute_update(Context* ctx, UpdateStmt* stmt) {
    Table* table = catalog_find_table(ctx, stmt->table_name);
    if (table == NULL) return 0;

    Row* rows = NULL;
    int row_count = 0;
    if (!read_all_rows(ctx, table, &rows, &row_count)) return 0;

    int set_col_index = -1;
    for (int i = 0; i < table->column_count; i++) {
        if (strcmp(table->columns[i].name, stmt->set_column) == 0) {
            set_col_index = i;
            break;
        }
    }
    if (set_col_index < 0) {
        for (int i = 0; i < row_count; i++) {
            for (int j = 0; j < rows[i].field_count; j++) {
                free(rows[i].fields[j].name);
                if (rows[i].fields[j].value.type == VAL_STRING) {
                    free(rows[i].fields[j].value.as.as_string);
                }
            }
            free(rows[i].fields);
        }
        free(rows);
        return 0;
    }

    for (int i = 0; i < row_count; i++) {
        if (stmt->has_where && !row_matches_where(&rows[i], stmt->where_column, stmt->where_op, &stmt->where_value)) {
            continue;
        }
        Cell* cell = &rows[i].fields[set_col_index].value;
        if (cell->type == VAL_STRING && cell->as.as_string != NULL) {
            free(cell->as.as_string);
        }
        cell->type = stmt->set_value.type;
        if (cell->type == VAL_STRING) {
            cell->as.as_string = stmt->set_value.as.as_string != NULL
                ? strdup(stmt->set_value.as.as_string)
                : strdup("");
        } else if (cell->type == VAL_FLOAT) {
            cell->as.as_float = stmt->set_value.as.as_float;
        } else {
            cell->as.as_int = stmt->set_value.as.as_int;
        }
    }

    free_row_pages(ctx, table);
    for (int i = 0; i < row_count; i++) {
        Cell* cells = malloc((size_t)rows[i].field_count * sizeof(Cell));
        if (cells == NULL) {
            for (int k = i; k < row_count; k++) {
                for (int j = 0; j < rows[k].field_count; j++) {
                    free(rows[k].fields[j].name);
                    if (rows[k].fields[j].value.type == VAL_STRING) {
                        free(rows[k].fields[j].value.as.as_string);
                    }
                }
                free(rows[k].fields);
            }
            free(rows);
            return 0;
        }
        for (int j = 0; j < rows[i].field_count; j++) {
            cells[j] = rows[i].fields[j].value;
        }
        catalog_insert(ctx, table, cells);
        free(cells);
    }

    for (int i = 0; i < row_count; i++) {
        for (int j = 0; j < rows[i].field_count; j++) {
            free(rows[i].fields[j].name);
            if (rows[i].fields[j].value.type == VAL_STRING) {
                free(rows[i].fields[j].value.as.as_string);
            }
        }
        free(rows[i].fields);
    }
    free(rows);

    catalog_write_page(ctx);
    return 1;
}

static int execute_delete(Context* ctx, DeleteStmt* stmt) {
    Table* table = catalog_find_table(ctx, stmt->table_name);
    if (table == NULL) return 0;

    Row* rows = NULL;
    int row_count = 0;
    if (!read_all_rows(ctx, table, &rows, &row_count)) return 0;

    free_row_pages(ctx, table);
    for (int i = 0; i < row_count; i++) {
        if (stmt->has_where && !row_matches_where(&rows[i], stmt->where_column, stmt->where_op, &stmt->where_value)) {
            Cell* cells = malloc((size_t)rows[i].field_count * sizeof(Cell));
            if (cells == NULL) {
                for (int k = i; k < row_count; k++) {
                    for (int j = 0; j < rows[k].field_count; j++) {
                        free(rows[k].fields[j].name);
                        if (rows[k].fields[j].value.type == VAL_STRING) {
                            free(rows[k].fields[j].value.as.as_string);
                        }
                    }
                    free(rows[k].fields);
                }
                free(rows);
                return 0;
            }
            for (int j = 0; j < rows[i].field_count; j++) {
                cells[j] = rows[i].fields[j].value;
            }
            catalog_insert(ctx, table, cells);
            free(cells);
        }
    }

    for (int i = 0; i < row_count; i++) {
        for (int j = 0; j < rows[i].field_count; j++) {
            free(rows[i].fields[j].name);
            if (rows[i].fields[j].value.type == VAL_STRING) {
                free(rows[i].fields[j].value.as.as_string);
            }
        }
        free(rows[i].fields);
    }
    free(rows);

    catalog_write_page(ctx);
    return 1;
}

/* -------------------------------------------------------------------------- */
/* Column type resolution                                                     */
/* -------------------------------------------------------------------------- */

static int is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

static const char* skip_space(const char* p) { while (*p && is_space(*p)) p++; return p; }

static int starts_with_ci(const char* p, const char* word) {
    const char* w = word;
    while (*w) {
        if (tolower((unsigned char)*p) != tolower((unsigned char)*w)) return 0;
        p++; w++;
    }
    return 1;
}

int sql_query_column_type(Context* ctx, const char* query, const char* column_name, int* out_type) {
    if (ctx == NULL || query == NULL || column_name == NULL || out_type == NULL) return 0;

    const char* p = skip_space(query);
    if (!starts_with_ci(p, "SELECT")) return 0;
    p += 6;
    p = skip_space(p);
    if (*p == '\0') return 0;

    /* Locate FROM as a whole word. */
    const char* from_pos = NULL;
    const char* q = p;
    while (*q) {
        if (starts_with_ci(q, "FROM")) {
            int preceded_by_ws = (q == p) || is_space(*(q - 1));
            int followed_by_ws = is_space(*(q + 4)) || *(q + 4) == '\0';
            if (preceded_by_ws && followed_by_ws) {
                from_pos = q;
                break;
            }
        }
        q++;
    }
    if (from_pos == NULL) return 0;

    /* Parse comma-separated selected columns between SELECT and FROM. */
    int has_star = 0;
    int selected = 0;
    const char* s = p;
    while (s < from_pos) {
        s = skip_space(s);
        const char* tok_start = s;
        while (s < from_pos && !is_space(*s) && *s != ',') s++;
        const char* tok_end = s;
        while (tok_end > tok_start && is_space(*(tok_end - 1))) tok_end--;

        int tok_len = (int)(tok_end - tok_start);
        if (tok_len > 0) {
            if (tok_len == 1 && *tok_start == '*') {
                has_star = 1;
            } else if (tok_len == (int)strlen(column_name) &&
                       strncasecmp(tok_start, column_name, tok_len) == 0) {
                selected = 1;
            }
        }

        s = skip_space(s);
        if (s < from_pos && *s == ',') {
            s++;
        }
    }

    /* Extract table name after FROM. */
    const char* t = from_pos + 4;
    t = skip_space(t);
    char table_name[MAX_NAME_LEN + 1];
    int i = 0;
    while (*t && !is_space(*t) && i < MAX_NAME_LEN) {
        table_name[i++] = *t++;
    }
    table_name[i] = '\0';
    if (table_name[0] == '\0') return 0;

    Table* table = catalog_find_table(ctx, table_name);
    if (table == NULL) return 0;
    if (!has_star && !selected) return 0;

    for (int c = 0; c < table->column_count; c++) {
        if (strcmp(table->columns[c].name, column_name) == 0) {
            *out_type = table->columns[c].type;
            return 1;
        }
    }
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Public query API                                                           */
/* -------------------------------------------------------------------------- */

Result* sql_exec(const char* query, Context* ctx) {
    if (ctx == NULL || ctx->pager == NULL) return NULL;

    SelectStmt stmt;
    if (!sql_parse_select(query, &stmt)) {
        return result_create(0);
    }

    Result* res = execute_select(ctx, &stmt);
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

/* -------------------------------------------------------------------------- */
/* Custom engine DBDriver implementation                                      */
/* -------------------------------------------------------------------------- */

typedef struct {
    Context ctx;
} CustomDriverImpl;

static int custom_open(DBDriver* driver, const char* connection_string) {
    CustomDriverImpl* impl = malloc(sizeof(CustomDriverImpl));
    if (impl == NULL) {
        snprintf(driver->error_message, sizeof(driver->error_message), "out of memory");
        return 0;
    }
    impl->ctx.db_path = connection_string != NULL ? connection_string : "mypl.db";
    impl->ctx.pager = NULL;
    if (!catalog_open(&impl->ctx)) {
        snprintf(driver->error_message, sizeof(driver->error_message),
                 "could not open catalog: %s", impl->ctx.db_path);
        free(impl);
        return 0;
    }
    driver->impl = impl;
    driver->error_message[0] = '\0';
    return 1;
}

static void custom_close(DBDriver* driver) {
    CustomDriverImpl* impl = (CustomDriverImpl*)driver->impl;
    if (impl == NULL) return;
    catalog_close(&impl->ctx);
    free(impl);
    driver->impl = NULL;
}

static int custom_exec(DBDriver* driver, const char* sql, Value* params, int param_count) {
    (void)params; (void)param_count;
    CustomDriverImpl* impl = (CustomDriverImpl*)driver->impl;
    int ok = sql_exec_ddl(sql, &impl->ctx);
    if (!ok) {
        snprintf(driver->error_message, sizeof(driver->error_message),
                 "custom engine: could not execute '%s'", sql);
    } else {
        driver->error_message[0] = '\0';
    }
    return ok;
}

static int custom_query(DBDriver* driver, const char* sql, Value* params, int param_count, void** result_handle) {
    (void)params; (void)param_count;
    CustomDriverImpl* impl = (CustomDriverImpl*)driver->impl;
    Result* res = sql_exec(sql, &impl->ctx);
    if (res == NULL) {
        snprintf(driver->error_message, sizeof(driver->error_message),
                 "custom engine: could not execute '%s'", sql);
        return 0;
    }
    driver->error_message[0] = '\0';
    *result_handle = res;
    return 1;
}

static int custom_result_next(DBDriver* driver, void* result_handle, void** row_handle) {
    (void)driver;
    Row* row = result_next((Result*)result_handle);
    if (row == NULL) return 0;
    *row_handle = row;
    return 1;
}

static int custom_row_get_field(DBDriver* driver, void* row_handle, const char* name, Value* out) {
    Cell cell = row_get_field((Row*)row_handle, name);
    switch (cell.type) {
        case VAL_INT:    *out = value_int(cell.as.as_int);       break;
        case VAL_FLOAT:  *out = value_float(cell.as.as_float);   break;
        case VAL_STRING: *out = value_string(strdup(cell.as.as_string)); break;
        default:
            snprintf(driver->error_message, sizeof(driver->error_message),
                     "column '%s' not found", name);
            *out = value_int(0);
            return 0;
    }
    driver->error_message[0] = '\0';
    return 1;
}

static int custom_row_get_column(DBDriver* driver, void* row_handle, int index, Value* out) {
    (void)driver;
    Row* row = (Row*)row_handle;
    if (row == NULL || index < 0 || index >= row->field_count) {
        *out = value_int(0);
        return 0;
    }
    Cell cell = row->fields[index].value;
    switch (cell.type) {
        case VAL_INT:    *out = value_int(cell.as.as_int);       break;
        case VAL_FLOAT:  *out = value_float(cell.as.as_float);   break;
        case VAL_STRING: *out = value_string(strdup(cell.as.as_string)); break;
        default:
            *out = value_int(0);
            return 0;
    }
    return 1;
}

static int custom_result_column_count(DBDriver* driver, void* result_handle) {
    (void)driver;
    Result* res = (Result*)result_handle;
    if (res == NULL || res->row_count == 0) return 0;
    return res->rows[0].field_count;
}

static const char* custom_result_column_name(DBDriver* driver, void* result_handle, int index) {
    (void)driver;
    Result* res = (Result*)result_handle;
    if (res == NULL || res->row_count == 0) return NULL;
    Row* row = &res->rows[0];
    if (index < 0 || index >= row->field_count) return NULL;
    return row->fields[index].name;
}

static void custom_result_free(DBDriver* driver, void* result_handle) {
    (void)driver;
    result_free((Result*)result_handle);
}

static int custom_begin(DBDriver* driver) {
    (void)driver;
    return 0; /* not supported by custom engine */
}

static int custom_commit(DBDriver* driver) {
    (void)driver;
    return 0; /* not supported by custom engine */
}

static int custom_rollback(DBDriver* driver) {
    (void)driver;
    return 0; /* not supported by custom engine */
}

void custom_driver_init(DBDriver* driver) {
    driver->impl = NULL;
    driver->open = custom_open;
    driver->close = custom_close;
    driver->exec = custom_exec;
    driver->query = custom_query;
    driver->result_next = custom_result_next;
    driver->row_get_field = custom_row_get_field;
    driver->row_get_column = custom_row_get_column;
    driver->result_column_count = custom_result_column_count;
    driver->result_column_name = custom_result_column_name;
    driver->result_free = custom_result_free;
    driver->begin = custom_begin;
    driver->commit = custom_commit;
    driver->rollback = custom_rollback;
}
