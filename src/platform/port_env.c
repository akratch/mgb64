/*
 * port_env.c — implementation of the registering GE007_* flag accessors.
 * See port_env.h for the rationale and semantics.
 *
 * The registry is a small fixed-capacity table, appended to lazily the first
 * time each named flag is accessed. Lookups are a linear scan by name — fine for
 * the read-once pattern these gates use (and hot call sites already cache their
 * own result in a static). Not thread-safe: call from the main thread, which is
 * where these flags are read.
 */
#include "port_env.h"

#include <stdlib.h>
#include <string.h>

typedef enum { K_BOOL, K_INT, K_FLOAT } Kind;

typedef struct {
    const char *name; /* caller's string literal (must be static/stable) */
    const char *help;
    Kind kind;
    int  parsed;   /* value has been read from the environment */
    int  was_set;  /* the environment variable was present */
    union { int b; int i; float f; } def;
    union { int b; int i; float f; } cur;
} Entry;

#define PORT_ENV_MAX 1024
static Entry s_entries[PORT_ENV_MAX];
static int   s_count = 0;

static Entry *find_entry(const char *name) {
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_entries[i].name, name) == 0) {
            return &s_entries[i];
        }
    }
    return NULL;
}

static Entry *get_or_create(const char *name, Kind kind, const char *help) {
    Entry *e = find_entry(name);
    if (e != NULL) {
        return e;
    }
    if (s_count >= PORT_ENV_MAX) {
        return NULL; /* registry full: caller falls back to a direct parse */
    }
    e = &s_entries[s_count++];
    memset(e, 0, sizeof(*e));
    e->name = name;
    e->help = (help != NULL) ? help : "";
    e->kind = kind;
    return e;
}

int port_env_bool(const char *name, int default_on, const char *help) {
    const char *v;
    Entry *e = get_or_create(name, K_BOOL, help);
    if (e == NULL) {
        v = getenv(name);
        if (v == NULL || v[0] == '\0') {
            return default_on ? 1 : 0;
        }
        return (v[0] == '0') ? 0 : 1;
    }
    if (!e->parsed) {
        v = getenv(name);
        e->was_set = (v != NULL);
        e->def.b = default_on ? 1 : 0;
        if (v == NULL || v[0] == '\0') {
            e->cur.b = default_on ? 1 : 0;
        } else {
            e->cur.b = (v[0] == '0') ? 0 : 1;
        }
        e->parsed = 1;
    }
    return e->cur.b;
}

int port_env_int(const char *name, int default_val, const char *help) {
    const char *v;
    char *end;
    long parsed;
    Entry *e = get_or_create(name, K_INT, help);
    if (e == NULL) {
        v = getenv(name);
        if (v == NULL || v[0] == '\0') {
            return default_val;
        }
        parsed = strtol(v, &end, 0);
        return (end == v) ? default_val : (int)parsed;
    }
    if (!e->parsed) {
        v = getenv(name);
        e->was_set = (v != NULL);
        e->def.i = default_val;
        if (v == NULL || v[0] == '\0') {
            e->cur.i = default_val;
        } else {
            parsed = strtol(v, &end, 0);
            e->cur.i = (end == v) ? default_val : (int)parsed;
        }
        e->parsed = 1;
    }
    return e->cur.i;
}

float port_env_float(const char *name, float default_val, const char *help) {
    const char *v;
    char *end;
    double parsed;
    Entry *e = get_or_create(name, K_FLOAT, help);
    if (e == NULL) {
        v = getenv(name);
        if (v == NULL || v[0] == '\0') {
            return default_val;
        }
        parsed = strtod(v, &end);
        return (end == v) ? default_val : (float)parsed;
    }
    if (!e->parsed) {
        v = getenv(name);
        e->was_set = (v != NULL);
        e->def.f = default_val;
        if (v == NULL || v[0] == '\0') {
            e->cur.f = default_val;
        } else {
            parsed = strtod(v, &end);
            e->cur.f = (end == v) ? default_val : (float)parsed;
        }
        e->parsed = 1;
    }
    return e->cur.f;
}

int port_env_registered_count(void) {
    return s_count;
}

static int entry_cmp(const void *a, const void *b) {
    return strcmp(((const Entry *)a)->name, ((const Entry *)b)->name);
}

static void fmt_values(const Entry *e, char *defbuf, char *curbuf, size_t n,
                       const char **type) {
    switch (e->kind) {
        case K_BOOL:
            *type = "bool";
            snprintf(defbuf, n, "%s", e->def.b ? "on" : "off");
            snprintf(curbuf, n, "%s", e->cur.b ? "on" : "off");
            break;
        case K_INT:
            *type = "int";
            snprintf(defbuf, n, "%d", e->def.i);
            snprintf(curbuf, n, "%d", e->cur.i);
            break;
        case K_FLOAT:
            *type = "float";
            snprintf(defbuf, n, "%g", (double)e->def.f);
            snprintf(curbuf, n, "%g", (double)e->cur.f);
            break;
        default:
            *type = "?";
            defbuf[0] = curbuf[0] = '\0';
            break;
    }
}

void port_env_dump(FILE *out, const char *format) {
    int md = (format != NULL && strcmp(format, "md") == 0);

    /* Sort a copy by name for stable, diffable output. */
    Entry *sorted = (Entry *)malloc(sizeof(Entry) * (size_t)(s_count > 0 ? s_count : 1));
    if (sorted == NULL) {
        return;
    }
    memcpy(sorted, s_entries, sizeof(Entry) * (size_t)s_count);
    qsort(sorted, (size_t)s_count, sizeof(Entry), entry_cmp);

    if (md) {
        fprintf(out, "| Flag | Type | Default | Current | Set | Description |\n");
        fprintf(out, "| --- | --- | --- | --- | --- | --- |\n");
    } else {
        fprintf(out, "%-46s %-6s %-9s %-9s %-4s %s\n",
                "FLAG", "TYPE", "DEFAULT", "CURRENT", "SET", "DESCRIPTION");
    }

    for (int i = 0; i < s_count; i++) {
        const Entry *e = &sorted[i];
        char defbuf[32];
        char curbuf[32];
        const char *type = "?";
        fmt_values(e, defbuf, curbuf, sizeof(defbuf), &type);
        if (md) {
            fprintf(out, "| `%s` | %s | %s | %s | %s | %s |\n",
                    e->name, type, defbuf, curbuf, e->was_set ? "yes" : "no", e->help);
        } else {
            fprintf(out, "%-46s %-6s %-9s %-9s %-4s %s\n",
                    e->name, type, defbuf, curbuf, e->was_set ? "yes" : "no", e->help);
        }
    }

    free(sorted);
}
