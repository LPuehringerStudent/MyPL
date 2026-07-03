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
    TOK_SELECT,
    TOK_FROM,
    TOK_WHERE,
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

typedef struct {
    char column_names[MAX_SELECT_COLUMNS][MAX_NAME_LEN + 1];
    int  column_count;
    char table_name[MAX_NAME_LEN + 1];
    int  has_where;
    char where_column[MAX_NAME_LEN + 1];
    int  where_op;
    Cell where_value;
} SelectStmt;

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

static Result* execute_select(Context* ctx, SelectStmt* stmt) {
    Table* table = catalog_find_table(ctx, stmt->table_name);
    if (table == NULL) {
        return result_create(0);
    }

    Row* rows = NULL;
    int row_count = 0;
    if (!read_all_rows(ctx, table, &rows, &row_count)) {
        return result_create(0);
    }

    Result* res = result_create(row_count);
    if (res == NULL) {
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
        return NULL;
    }

    for (int i = 0; i < row_count; i++) {
        if (stmt->has_where && !evaluate_where(&rows[i], stmt)) {
            continue;
        }
        result_append(res, &rows[i], table, stmt);
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
