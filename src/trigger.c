#include <string.h>

#include "trigger.h"
#include "ast.h"

int sql_trigger_info(const char* sql, int* event, char* table, size_t table_size) {
    const char* p = sql;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    char kw[16];
    int ki = 0;
    while (*p && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) && ki < 15) {
        char c = *p++;
        kw[ki++] = (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
    }
    kw[ki] = '\0';

    int ev = -1;
    const char* skip_kw = NULL;
    if (strcmp(kw, "insert") == 0)      { ev = TRIGGER_INSERT; skip_kw = "into"; }
    else if (strcmp(kw, "update") == 0) { ev = TRIGGER_UPDATE; skip_kw = NULL; }
    else if (strcmp(kw, "delete") == 0) { ev = TRIGGER_DELETE; skip_kw = "from"; }
    else if (strcmp(kw, "create") == 0) { ev = TRIGGER_CREATE; skip_kw = "table"; }
    else if (strcmp(kw, "drop") == 0)   { ev = TRIGGER_DROP;   skip_kw = "table"; }
    if (ev < 0) return 0;

    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (skip_kw != NULL) {
        size_t n = strlen(skip_kw);
        for (size_t i = 0; i < n; i++) {
            char c = p[i];
            char lower = (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
            if (lower != skip_kw[i]) return 0;
        }
        p += n;
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    }
    size_t ti = 0;
    while (*p && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                  (*p >= '0' && *p <= '9') || *p == '_') && ti + 1 < table_size) {
        table[ti++] = *p++;
    }
    if (ti == 0) return 0;
    table[ti] = '\0';
    *event = ev;
    return 1;
}

int trigger_name_equals(const char* a, const char* b) {
    while (*a && *b) {
        char ca = *a++;
        char cb = *b++;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return 0;
    }
    return *a == '\0' && *b == '\0';
}

static const char* skip_sql_ws(const char* p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

/* Match a case-insensitive keyword at *pp; on success advance past it and
 * any following whitespace. The keyword must be followed by a non-identifier
 * character. */
static int match_sql_keyword(const char** pp, const char* kw) {
    const char* p = *pp;
    size_t n = strlen(kw);
    for (size_t i = 0; i < n; i++) {
        char c = p[i];
        char lower = (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
        if (lower != kw[i]) return 0;
    }
    char end = p[n];
    if ((end >= 'a' && end <= 'z') || (end >= 'A' && end <= 'Z') ||
        (end >= '0' && end <= '9') || end == '_') return 0;
    *pp = skip_sql_ws(p + n);
    return 1;
}

int sql_drop_trigger_name(const char* sql, char* name, size_t name_size) {
    const char* p = skip_sql_ws(sql);
    if (!match_sql_keyword(&p, "drop")) return 0;
    if (!match_sql_keyword(&p, "trigger")) return 0;
    size_t ni = 0;
    while (*p && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                  (*p >= '0' && *p <= '9') || *p == '_')) {
        if (ni + 1 < name_size) name[ni++] = *p;
        p++;
    }
    if (ni == 0) return 0;
    name[ni] = '\0';
    p = skip_sql_ws(p);
    if (*p == ';') p = skip_sql_ws(p + 1);
    return *p == '\0';
}
