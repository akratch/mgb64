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
#include <limits.h>
#include "config_pc.h"
#include "settings.h"
#include "savedir.h"
#include <errno.h>
#ifdef _WIN32
#include <windows.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* ===== Types ===== */

typedef enum {
    CFG_NONE,
    CFG_S32,
    CFG_F32,
    CFG_U32,
    CFG_ENUM,
    CFG_STRING
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
    const ConfigEnumOption *enum_options;
    s32 enum_count;
    size_t string_capacity;
} ConfigEntry;

#define CONFIG_MAX_UNKNOWN_SETTINGS 128
#define CONFIG_MAX_VALUE_LENGTH 384

typedef struct {
    char key[CONFIG_MAX_KEYNAME + 1];   /* "Section.Key" */
    s32 seclen;                         /* length of section prefix */
    char value[CONFIG_MAX_VALUE_LENGTH + 1];
} ConfigUnknownEntry;

/* ===== State ===== */

static ConfigEntry s_entries[CONFIG_MAX_SETTINGS];
static s32 s_numEntries;
static ConfigUnknownEntry s_unknownEntries[CONFIG_MAX_UNKNOWN_SETTINGS];
static s32 s_numUnknownEntries;

/* ===== Helpers ===== */

static s32 clampInt(s32 val, s32 lo, s32 hi) {
    return (val < lo) ? lo : (val > hi) ? hi : val;
}
static f32 clampFloat(f32 val, f32 lo, f32 hi) {
    /* NaN compares false against everything, so both branches below fall
     * through and NaN is stored verbatim (a stray "nan" in an ini/env/CLI
     * value is enough -- strtof() accepts the literal). Downstream float->int
     * conversions of a stored NaN are UB. Treat NaN as "below range". */
    if (val != val) return lo;
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
    snprintf(e->key, sizeof(e->key), "%s", key);
    const char *dot = strrchr(e->key, '.');
    e->seclen = dot ? (s32)(dot - e->key) : 0;
    return e;
}

static ConfigEntry *findOrAddEntry(const char *key) {
    ConfigEntry *e = findEntry(key);
    return e ? e : addEntry(key);
}

static ConfigUnknownEntry *findUnknownEntry(const char *key) {
    for (s32 i = 0; i < s_numUnknownEntries; i++) {
        if (strcasecmp(s_unknownEntries[i].key, key) == 0)
            return &s_unknownEntries[i];
    }
    return NULL;
}

static void rememberUnknownEntry(const char *key, const char *value) {
    ConfigUnknownEntry *e = findUnknownEntry(key);

    if (!e) {
        if (s_numUnknownEntries >= CONFIG_MAX_UNKNOWN_SETTINGS) return;
        e = &s_unknownEntries[s_numUnknownEntries++];
        memset(e, 0, sizeof(*e));
        snprintf(e->key, sizeof(e->key), "%s", key);
        const char *dot = strrchr(e->key, '.');
        e->seclen = dot ? (s32)(dot - e->key) : 0;
    }

    snprintf(e->value, sizeof(e->value), "%s", value);
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

void configRegisterEnum(const char *key, s32 *var,
                        const ConfigEnumOption *options, s32 option_count)
{
    ConfigEntry *e = findOrAddEntry(key);
    if (e) {
        e->type = CFG_ENUM;
        e->ptr = var;
        e->enum_options = options;
        e->enum_count = option_count;
    }
}

void configRegisterString(const char *key, char *var, size_t capacity)
{
    ConfigEntry *e = findOrAddEntry(key);
    if (e) {
        e->type = CFG_STRING;
        e->ptr = var;
        e->string_capacity = capacity;
    }
}

/* ===== Set from string (INI parsing) ===== */

static s32 setFromString(const char *key, const char *val)
{
    ConfigEntry *e = findEntry(key);
    if (!e) return 0;

    switch (e->type) {
        case CFG_S32: {
            s32 v = (s32)strtol(val, NULL, 0);
            if (e->min_s32 < e->max_s32) v = clampInt(v, e->min_s32, e->max_s32);
            *(s32 *)e->ptr = v;
        } break;
        case CFG_F32: {
            f32 v = strtof(val, NULL);
            if (v != v) {
                /* strtof() happily parses the literal "nan". Clamping NaN to
                 * the range floor would be silently wrong for settings whose
                 * floor is a "muted"/"off" value (e.g. Audio.MasterVolume's
                 * floor is 0.0) -- so reject the NaN outright and keep
                 * whatever value the setting already had (its compiled-in
                 * default, or an earlier valid override), rather than storing
                 * NaN or clamping to an unrelated sentinel. */
                fprintf(stderr,
                        "[config] WARNING: '%s=%s' is not a finite number; keeping previous value.\n",
                        key, val);
                break;
            }
            if (e->min_f32 < e->max_f32) v = clampFloat(v, e->min_f32, e->max_f32);
            *(f32 *)e->ptr = v;
        } break;
        case CFG_U32: {
            u32 v = (u32)strtoul(val, NULL, 0);
            if (e->min_u32 < e->max_u32) v = clampUInt(v, e->min_u32, e->max_u32);
            *(u32 *)e->ptr = v;
        } break;
        case CFG_ENUM: {
            s32 found = 0;
            for (s32 i = 0; i < e->enum_count; i++) {
                if (e->enum_options[i].token &&
                    strcasecmp(e->enum_options[i].token, val) == 0) {
                    *(s32 *)e->ptr = e->enum_options[i].value;
                    found = 1;
                    break;
                }
            }
            if (!found) {
                /* Not a known token. Some enum settings are also historically
                 * written as their raw backing number (e.g. "SSAO.Mode=2"), so
                 * accept a numeric value IF it equals one of the option's actual
                 * `.value`s. Enum backing values are NOT contiguous indices
                 * (Video.MSAA is 0/2/4/8, Video.FrameCap is 0/30/60,
                 * Video.SsaoMode is 1/2) -- clamping an out-of-range number into
                 * [0, enum_count-1] would silently select a different (but
                 * "in range"-looking) option than anything requested, which is
                 * worse than doing nothing. That differs from the CFG_S32/U32
                 * cases above, where the domain really is a contiguous range and
                 * "nearest valid value" is a sane clamp target; for an enum
                 * there is no sane "nearest", so an unmatched value is rejected
                 * outright and the previous value is kept, matching the CFG_F32
                 * NaN-rejection philosophy just above. */
                char *end = NULL;
                long numeric = strtol(val, &end, 0);
                s32 matched = 0;
                if (end != val) { /* strtol actually consumed a number */
                    for (s32 i = 0; i < e->enum_count; i++) {
                        if (e->enum_options[i].value == (s32)numeric) {
                            *(s32 *)e->ptr = (s32)numeric;
                            matched = 1;
                            break;
                        }
                    }
                }
                if (!matched) {
                    fprintf(stderr,
                            "[config] WARNING: '%s=%s' is not a recognized option; "
                            "keeping previous value.\n",
                            key, val);
                }
            }
        } break;
        case CFG_STRING: {
            if (e->ptr && e->string_capacity > 0) {
                snprintf((char *)e->ptr, e->string_capacity, "%s", val);
            }
        } break;
        default: break;
    }

    return 1;
}

/* ===== Load ===== */

static s32 configLoad(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    /* TexturePack (a CFG_STRING) has a 1024-byte capacity; a 512-byte line
     * buffer truncates/splits a long path across two fgets() calls, corrupting
     * the value. 2048 covers the longest registered string setting with
     * headroom. */
    char line[2048];
    char curSection[CONFIG_MAX_SECNAME + 1] = {0};
    s_numUnknownEntries = 0;

    /* §4.3 MasterVolume migration bookkeeping: track whether this ini
     * explicitly set these two keys (as opposed to them being left at
     * compiled-in defaults), so we can distinguish a genuine pre-bus install
     * from a deliberately-set post-bus 0.7 after the loop below. */
    s32 sawMasterVolumeKey = 0;
    s32 sawSfxVolumeKey = 0;

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

        if (strcmp(fullKey, "Audio.MasterVolume") == 0) {
            sawMasterVolumeKey = 1;
        } else if (strcmp(fullKey, "Audio.SfxVolume") == 0) {
            sawSfxVolumeKey = 1;
        }

        if (!setFromString(fullKey, valPart)) {
            rememberUnknownEntry(fullKey, valPart);
        }
    }

    fclose(f);

    /* §4.3: one-time migration for stale pre-W6.E3.T1 installs. Before the
     * volume-bus merge, Audio.MasterVolume was a dead setting persisted at
     * its old default of 0.7; the merge made it live, so an untouched old
     * ini would suddenly apply a real -3.1dB attenuation on update. The
     * pre-bus ini format never wrote Audio.SfxVolume (that key didn't exist
     * yet), so "MasterVolume is exactly the old dead default, and SfxVolume
     * is absent" is specific enough to not fire on a deliberately-set
     * post-bus 0.7 (which would coexist with a saved SfxVolume key, since
     * configSave() always writes every registered setting). */
    if (sawMasterVolumeKey && !sawSfxVolumeKey) {
        ConfigEntry *e = findEntry("Audio.MasterVolume");
        if (e != NULL && e->type == CFG_F32 && *(f32 *)e->ptr == 0.7f) {
            *(f32 *)e->ptr = 1.0f;
            fprintf(stderr,
                    "[config] Migrated stale pre-bus Audio.MasterVolume=0.7 to the "
                    "post-bus unity default (1.0). The old value was a no-op "
                    "placeholder before the music/SFX volume-bus merge; it is live "
                    "now and 0.7 would silently attenuate audio, so this one-time "
                    "migration runs once per install to restore unity gain.\n");
        }
    }

    return 1;
}

/* ===== Save ===== */

static void formatSettingDefault(const Setting *setting, char *out, size_t out_size) {
    switch (setting->type) {
        case SETTING_TYPE_INT:
            snprintf(out, out_size, "%d", setting->def.s32_value);
            break;
        case SETTING_TYPE_UINT:
            snprintf(out, out_size, "%u", setting->def.u32_value);
            break;
        case SETTING_TYPE_FLOAT:
            snprintf(out, out_size, "%g", (double)setting->def.f32_value);
            break;
        case SETTING_TYPE_ENUM:
            snprintf(out, out_size, "%s", settingsEnumTokenForValue(setting, setting->def.s32_value));
            break;
        case SETTING_TYPE_STRING:
            snprintf(out, out_size, "%s", setting->def_string ? setting->def_string : "");
            break;
        default:
            snprintf(out, out_size, "?");
            break;
    }
}

static void formatSettingRange(const Setting *setting, char *out, size_t out_size) {
    switch (setting->type) {
        case SETTING_TYPE_INT:
            snprintf(out, out_size, "%d..%d", setting->min.s32_value, setting->max.s32_value);
            break;
        case SETTING_TYPE_UINT:
            snprintf(out, out_size, "%u..%u", setting->min.u32_value, setting->max.u32_value);
            break;
        case SETTING_TYPE_FLOAT:
            snprintf(out, out_size, "%g..%g",
                     (double)setting->min.f32_value,
                     (double)setting->max.f32_value);
            break;
        case SETTING_TYPE_ENUM:
            settingsFormatEnumOptions(setting, out, out_size);
            break;
        case SETTING_TYPE_STRING:
            snprintf(out, out_size, "len<%u", (unsigned int)setting->string_capacity);
            break;
        default:
            snprintf(out, out_size, "?");
            break;
    }
}

static void saveEntryMetadataComment(ConfigEntry *e, FILE *f) {
    const Setting *setting = settingsFind(e->key);
    char def[64];
    char range[64];

    if (!setting) {
        return;
    }

    formatSettingDefault(setting, def, sizeof(def));
    formatSettingRange(setting, range, sizeof(range));

    if (setting->label) {
        fprintf(f, "# %s\n", setting->label);
    }
    if (setting->help) {
        fprintf(f, "# %s\n", setting->help);
    }
    fprintf(f, "# type=%s scope=%s default=%s range=%s\n",
            settingsTypeName(setting->type),
            settingsScopeName(setting->scope),
            def,
            range);
}

static void saveEntry(ConfigEntry *e, FILE *f) {
    const char *keyName = e->key + e->seclen + (e->seclen > 0 ? 1 : 0);
    /* Transient env overrides (GE007_*) apply to this launch only and must NOT be
     * persisted -- otherwise a one-off e.g. GE007_TEXTURE_PACK=... would silently
     * stick on every later launch. Omit them so the saved config keeps the user's
     * own (menu/file) value; on reload an absent key falls back to its default.
     * CLI --config-override is intentionally still saved (config-pinning). */
    {
        const Setting *st = settingsFind(e->key);
        if (st != NULL && st->override_source == SETTING_OVERRIDE_ENV) {
            return;
        }
    }
    saveEntryMetadataComment(e, f);
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
        case CFG_ENUM: {
            s32 v = *(s32 *)e->ptr;
            const char *token = NULL;
            for (s32 i = 0; i < e->enum_count; i++) {
                if (e->enum_options[i].value == v) {
                    token = e->enum_options[i].token;
                    break;
                }
            }
            if (token) {
                fprintf(f, "%s=%s\n", keyName, token);
            } else {
                fprintf(f, "%s=%d\n", keyName, v);
            }
        } break;
        case CFG_STRING:
            fprintf(f, "%s=%s\n", keyName, e->ptr ? (char *)e->ptr : "");
            break;
        default: break;
    }
}

static const char *configKeyName(const char *key, s32 seclen) {
    return key + seclen + (seclen > 0 ? 1 : 0);
}

static void configSectionName(const char *key, s32 seclen, char *out, size_t out_size) {
    if (out_size == 0) return;

    if (seclen > 0 && seclen <= CONFIG_MAX_SECNAME) {
        memcpy(out, key, (size_t)seclen);
        out[seclen] = '\0';
    } else {
        snprintf(out, out_size, "General");
    }
}

static void saveUnknownEntry(ConfigUnknownEntry *e, FILE *f) {
    fprintf(f, "%s=%s\n", configKeyName(e->key, e->seclen), e->value);
}

static void saveUnknownSection(const char *section, FILE *f, u8 *written) {
    char sec[CONFIG_MAX_SECNAME + 1];

    for (s32 i = 0; i < s_numUnknownEntries; i++) {
        if (written[i]) continue;

        configSectionName(s_unknownEntries[i].key, s_unknownEntries[i].seclen, sec, sizeof(sec));
        if (strcmp(sec, section) == 0) {
            saveUnknownEntry(&s_unknownEntries[i], f);
            written[i] = 1;
        }
    }
}

static s32 replaceConfigFile(const char *tmp_path, const char *path) {
#ifdef _WIN32
    return MoveFileExA(tmp_path, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? 0 : -1;
#else
    return rename(tmp_path, path);
#endif
}

/* When set (by a `--faithful` launch), configSave() is a no-op: a faithful
 * session forces the pre-remaster baseline into the live settings for that run
 * only, so writing them back -- whether on clean shutdown, the in-game menu, or
 * --config-set/--reset-config -- would clobber the user's saved remaster config.
 * Suppressing the whole save keeps ge007.ini byte-for-byte untouched. */
static s32 s_configSaveSuppressed = 0;

void configSetSaveSuppressed(s32 suppressed)
{
    s_configSaveSuppressed = suppressed ? 1 : 0;
}

s32 configSave(void)
{
    char path[PATH_MAX];
    char tmp_path[PATH_MAX];

    if (s_configSaveSuppressed) {
        return 1; /* faithful session: ge007.ini intentionally left untouched */
    }
    u8 unknown_written[CONFIG_MAX_UNKNOWN_SETTINGS] = {0};
    const char *saved_path = savedirPath(CONFIG_FILENAME);
    snprintf(path, sizeof(path), "%s", saved_path);
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    FILE *f = fopen(tmp_path, "w");
    if (!f) {
        printf("[CONFIG] Failed to save %s: %s\n", tmp_path, strerror(errno));
        return 0;
    }

    fprintf(f, "# MGB64 -- N64 game engine port -- configuration\n");
    fprintf(f, "# Edit values below or delete this file to reset defaults.\n\n");

    char curSec[CONFIG_MAX_SECNAME + 1] = {0};
    for (s32 i = 0; i < s_numEntries; i++) {
        ConfigEntry *e = &s_entries[i];
        char sec[CONFIG_MAX_SECNAME + 1];
        configSectionName(e->key, e->seclen, sec, sizeof(sec));

        if (strcmp(curSec, sec) != 0) {
            if (curSec[0] != '\0') {
                saveUnknownSection(curSec, f, unknown_written);
            }
            fprintf(f, "\n[%s]\n", sec);
            snprintf(curSec, sizeof(curSec), "%s", sec);
        }
        saveEntry(e, f);
    }

    if (curSec[0] != '\0') {
        saveUnknownSection(curSec, f, unknown_written);
    }

    for (s32 i = 0; i < s_numUnknownEntries; i++) {
        char sec[CONFIG_MAX_SECNAME + 1];
        if (unknown_written[i]) continue;
        configSectionName(s_unknownEntries[i].key, s_unknownEntries[i].seclen, sec, sizeof(sec));
        fprintf(f, "\n[%s]\n", sec);
        saveUnknownEntry(&s_unknownEntries[i], f);
        unknown_written[i] = 1;
    }

    if (fclose(f) != 0) {
        printf("[CONFIG] Failed to finish writing %s: %s\n", tmp_path, strerror(errno));
        remove(tmp_path);
        return 0;
    }

#ifdef _WIN32
    if (replaceConfigFile(tmp_path, path) != 0) {
        printf("[CONFIG] Failed to replace %s: Windows error %lu\n", path, (unsigned long)GetLastError());
        remove(tmp_path);
        return 0;
    }
#else
    if (replaceConfigFile(tmp_path, path) != 0) {
        printf("[CONFIG] Failed to replace %s: %s\n", path, strerror(errno));
        remove(tmp_path);
        return 0;
    }
#endif

    printf("[CONFIG] Saved %s\n", path);
    return 1;
}

s32 configSetValue(const char *key, const char *value)
{
    if (!key || !value) {
        return 0;
    }

    return setFromString(key, value);
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
            case CFG_ENUM: *type_out = 3; break;
            case CFG_STRING: *type_out = 4; break;
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
