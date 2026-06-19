/**
 * config_pc.c — INI config file system for the GE007 PC port.
 *
 * Declarative registration + INI parsing/saving, modeled after the
 * Perfect Dark port's config.c. Settings are registered by pointer
 * with type and validation range, then loaded from ge007.ini.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "config_pc.h"
#include "savedir.h"
#include <errno.h>

/* ===== Types ===== */

typedef enum {
    CFG_NONE,
    CFG_S32,
    CFG_F32,
    CFG_U32
} ConfigType;

typedef struct {
    char key[CONFIG_MAX_KEYNAME + 1]; /* "Section.Key" */
    s32 seclen;                       /* length of section prefix */
    ConfigType type;
    void *ptr;
    union {
        struct { f32 min_f32, max_f32; };
        struct { s32 min_s32, max_s32; };
        struct { u32 min_u32, max_u32; };
    };
} ConfigEntry;

/* ===== State ===== */

static ConfigEntry s_entries[CONFIG_MAX_SETTINGS];
static s32 s_numEntries;

/* ===== Helpers ===== */

static s32 clampInt(s32 val, s32 lo, s32 hi) {
    return (val < lo) ? lo : (val > hi) ? hi : val;
}
static f32 clampFloat(f32 val, f32 lo, f32 hi) {
    return (val < lo) ? lo : (val > hi) ? hi : val;
}
static u32 clampUInt(u32 val, u32 lo, u32 hi) {
    return (val < lo) ? lo : (val > hi) ? hi : val;
}

static char *trimWhitespace(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    size_t len = strlen(s);
    if (len == 0) return s;
    char *end = s + len - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

static ConfigEntry *findEntry(const char *key) {
    for (s32 i = 0; i < s_numEntries; i++) {
        if (strcasecmp(s_entries[i].key, key) == 0)
            return &s_entries[i];
    }
    return NULL;
}

static ConfigEntry *addEntry(const char *key) {
    if (s_numEntries >= CONFIG_MAX_SETTINGS) return NULL;
    ConfigEntry *e = &s_entries[s_numEntries++];
    snprintf(e->key, CONFIG_MAX_KEYNAME, "%s", key);
    const char *dot = strrchr(e->key, '.');
    e->seclen = dot ? (s32)(dot - e->key) : 0;
    return e;
}

static ConfigEntry *findOrAddEntry(const char *key) {
    ConfigEntry *e = findEntry(key);
    return e ? e : addEntry(key);
}

/* ===== Registration ===== */

void configRegisterInt(const char *key, s32 *var, s32 min, s32 max)
{
    ConfigEntry *e = findOrAddEntry(key);
    if (e) { e->type = CFG_S32; e->ptr = var; e->min_s32 = min; e->max_s32 = max; }
}

void configRegisterFloat(const char *key, f32 *var, f32 min, f32 max)
{
    ConfigEntry *e = findOrAddEntry(key);
    if (e) { e->type = CFG_F32; e->ptr = var; e->min_f32 = min; e->max_f32 = max; }
}

void configRegisterUInt(const char *key, u32 *var, u32 min, u32 max)
{
    ConfigEntry *e = findOrAddEntry(key);
    if (e) { e->type = CFG_U32; e->ptr = var; e->min_u32 = min; e->max_u32 = max; }
}

/* ===== Set from string (INI parsing) ===== */

static void setFromString(const char *key, const char *val)
{
    ConfigEntry *e = findEntry(key);
    if (!e) return;

    switch (e->type) {
        case CFG_S32: {
            s32 v = (s32)strtol(val, NULL, 0);
            if (e->min_s32 < e->max_s32) v = clampInt(v, e->min_s32, e->max_s32);
            *(s32 *)e->ptr = v;
        } break;
        case CFG_F32: {
            f32 v = strtof(val, NULL);
            if (e->min_f32 < e->max_f32) v = clampFloat(v, e->min_f32, e->max_f32);
            *(f32 *)e->ptr = v;
        } break;
        case CFG_U32: {
            u32 v = (u32)strtoul(val, NULL, 0);
            if (e->min_u32 < e->max_u32) v = clampUInt(v, e->min_u32, e->max_u32);
            *(u32 *)e->ptr = v;
        } break;
        default: break;
    }
}

/* ===== Load ===== */

static s32 configLoad(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    char line[512];
    char curSection[CONFIG_MAX_SECNAME + 1] = {0};

    while (fgets(line, sizeof(line), f)) {
        char *p = trimWhitespace(line);
        if (!*p || *p == '#' || *p == ';') continue; /* comment or empty */

        if (*p == '[') {
            /* Section header: [SectionName] */
            char *end = strchr(p, ']');
            if (end) {
                *end = '\0';
                snprintf(curSection, CONFIG_MAX_SECNAME, "%s", p + 1);
            }
            continue;
        }

        /* Key=Value */
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';

        char *keyPart = trimWhitespace(p);
        char *valPart = trimWhitespace(eq + 1);

        char fullKey[CONFIG_MAX_KEYNAME + 1];
        if (curSection[0])
            snprintf(fullKey, CONFIG_MAX_KEYNAME, "%s.%s", curSection, keyPart);
        else
            snprintf(fullKey, CONFIG_MAX_KEYNAME, "%s", keyPart);

        setFromString(fullKey, valPart);
    }

    fclose(f);
    return 1;
}

/* ===== Save ===== */

static void saveEntry(ConfigEntry *e, FILE *f) {
    const char *keyName = e->key + e->seclen + (e->seclen > 0 ? 1 : 0);
    switch (e->type) {
        case CFG_S32: {
            s32 v = *(s32 *)e->ptr;
            if (e->min_s32 < e->max_s32) v = clampInt(v, e->min_s32, e->max_s32);
            fprintf(f, "%s=%d\n", keyName, v);
        } break;
        case CFG_F32: {
            f32 v = *(f32 *)e->ptr;
            if (e->min_f32 < e->max_f32) v = clampFloat(v, e->min_f32, e->max_f32);
            fprintf(f, "%s=%g\n", keyName, (double)v);
        } break;
        case CFG_U32: {
            u32 v = *(u32 *)e->ptr;
            if (e->min_u32 < e->max_u32) v = clampUInt(v, e->min_u32, e->max_u32);
            fprintf(f, "%s=%u\n", keyName, v);
        } break;
        default: break;
    }
}

s32 configSave(void)
{
    const char *path = savedirPath(CONFIG_FILENAME);
    FILE *f = fopen(path, "w");
    if (!f) {
        printf("[CONFIG] Failed to save %s: %s\n", path, strerror(errno));
        return 0;
    }

    fprintf(f, "# MGB64 -- N64 game engine port -- configuration\n");
    fprintf(f, "# Edit values below or delete this file to reset defaults.\n\n");

    char curSec[CONFIG_MAX_SECNAME + 1] = {0};
    for (s32 i = 0; i < s_numEntries; i++) {
        ConfigEntry *e = &s_entries[i];
        char sec[CONFIG_MAX_SECNAME + 1];
        if (e->seclen > 0 && e->seclen <= CONFIG_MAX_SECNAME) {
            memcpy(sec, e->key, e->seclen);
            sec[e->seclen] = '\0';
        } else {
            snprintf(sec, CONFIG_MAX_SECNAME, "%s", e->key);
        }

        if (strcmp(curSec, sec) != 0) {
            fprintf(f, "\n[%s]\n", sec);
            snprintf(curSec, CONFIG_MAX_SECNAME, "%s", sec);
        }
        saveEntry(e, f);
    }

    fclose(f);
    printf("[CONFIG] Saved %s\n", path);
    return 1;
}

/* ===== Public Lookup ===== */

void *configFindEntry(const char *key, int *type_out)
{
    ConfigEntry *e = findEntry(key);
    if (!e || e->type == CFG_NONE) {
        if (type_out) *type_out = -1;
        return NULL;
    }
    if (type_out) {
        switch (e->type) {
            case CFG_S32: *type_out = 0; break;
            case CFG_F32: *type_out = 1; break;
            case CFG_U32: *type_out = 2; break;
            default:      *type_out = -1; break;
        }
    }
    return e->ptr;
}

/* ===== Init ===== */

void configInit(void)
{
    const char *path = savedirPath(CONFIG_FILENAME);
    if (configLoad(path)) {
        printf("[CONFIG] Loaded %s (%d settings)\n", path, s_numEntries);
    } else {
        printf("[CONFIG] No config file found, writing defaults.\n");
        configSave();
    }
}
