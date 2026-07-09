// config_schema.c — engine-side implementation of the app config_schema API.
// Translates the Setting registry into plain-type entries for the app UI.
#include "../app/config_schema.h"

#include <string.h>
#include <stdio.h>

#include "settings.h"
#include "config_pc.h"
#include "savedir.h"

extern void platformRegisterConfig(void);
extern void portAudioRegisterConfig(void);

void mgb_config_init(void) {
    savedirInit(NULL);
    platformRegisterConfig();
    portAudioRegisterConfig();
    configInit();
}

int mgb_config_save(void) { return (int)configSave(); }

int mgb_config_count(void) { return (int)settingsCount(); }

static MgbCfgKind kindOf(SettingType t) {
    switch (t) {
        case SETTING_TYPE_INT:   return MGB_CFG_INT;
        case SETTING_TYPE_UINT:  return MGB_CFG_UINT;
        case SETTING_TYPE_FLOAT: return MGB_CFG_FLOAT;
        case SETTING_TYPE_ENUM:  return MGB_CFG_ENUM;
        default:                 return MGB_CFG_STRING;
    }
}

int mgb_config_get(int index, MgbCfgEntry *out) {
    const Setting *s = settingsAt((s32)index);
    if (!s || !out) return 0;
    memset(out, 0, sizeof(*out));

    snprintf(out->key, sizeof(out->key), "%s", s->key);
    // Split "Section.name".
    const char *dot = strchr(s->key, '.');
    if (dot) {
        size_t seclen = (size_t)(dot - s->key);
        if (seclen >= sizeof(out->section)) seclen = sizeof(out->section) - 1;
        memcpy(out->section, s->key, seclen);
        out->section[seclen] = '\0';
        snprintf(out->name, sizeof(out->name), "%s", dot + 1);
    } else {
        snprintf(out->section, sizeof(out->section), "%s", "General");
        snprintf(out->name, sizeof(out->name), "%s", s->key);
    }
    snprintf(out->label, sizeof(out->label), "%s", (s->label && s->label[0]) ? s->label : out->name);
    snprintf(out->help, sizeof(out->help), "%s", s->help ? s->help : "");

    out->kind = kindOf(s->type);
    out->is_live = (s->scope == SETTING_SCOPE_LIVE) ? 1 : 0;
    out->enum_count = (int)s->enum_count;
    out->advanced = s->advanced ? 1 : 0;

    switch (s->type) {
        case SETTING_TYPE_INT:
            out->min_val = (float)s->min.s32_value;
            out->max_val = (float)s->max.s32_value;
            out->def_val = (float)s->def.s32_value;
            out->cur_int = s->ptr ? *(s32 *)s->ptr : s->def.s32_value;
            break;
        case SETTING_TYPE_UINT:
            out->min_val = (float)s->min.u32_value;
            out->max_val = (float)s->max.u32_value;
            out->def_val = (float)s->def.u32_value;
            out->cur_int = s->ptr ? (int)*(u32 *)s->ptr : (int)s->def.u32_value;
            break;
        case SETTING_TYPE_FLOAT:
            out->min_val = s->min.f32_value;
            out->max_val = s->max.f32_value;
            out->def_val = s->def.f32_value;
            out->cur_float = s->ptr ? *(f32 *)s->ptr : s->def.f32_value;
            break;
        case SETTING_TYPE_ENUM: {
            out->min_val = 0.0f;
            out->max_val = (float)(s->enum_count > 0 ? s->enum_count - 1 : 0);
            s32 cur = s->ptr ? *(s32 *)s->ptr : s->def.s32_value;
            out->cur_int = cur;
            out->cur_enum_index = 0;
            for (s32 i = 0; i < s->enum_count; i++) {
                if (s->enum_options[i].value == cur) { out->cur_enum_index = (int)i; break; }
            }
            break;
        }
        default:
            break;
    }
    return 1;
}

int mgb_config_get_int(const char *key, int fallback) {
    const Setting *s = settingsFind(key);
    if (!s || !s->ptr) return fallback;
    switch (s->type) {
        case SETTING_TYPE_INT:
        case SETTING_TYPE_ENUM: return (int)*(s32 *)s->ptr;
        case SETTING_TYPE_UINT: return (int)*(u32 *)s->ptr;
        default:                return fallback;
    }
}

float mgb_config_get_float(const char *key, float fallback) {
    const Setting *s = settingsFind(key);
    if (!s || !s->ptr || s->type != SETTING_TYPE_FLOAT) return fallback;
    return *(f32 *)s->ptr;
}

int mgb_config_get_string(const char *key, char *out, int out_size) {
    if (!out || out_size <= 0) return 0;
    out[0] = '\0';
    const Setting *s = settingsFind(key);
    if (!s || s->type != SETTING_TYPE_STRING || !s->ptr) return 0;
    snprintf(out, (size_t)out_size, "%s", (const char *)s->ptr);
    return 1;
}

const char *mgb_config_enum_token(const char *key, int optIndex) {
    const Setting *s = settingsFind(key);
    if (!s || optIndex < 0 || optIndex >= s->enum_count) return "";
    return s->enum_options[optIndex].token ? s->enum_options[optIndex].token : "";
}

void mgb_config_set_int(const char *key, int value) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%d", value);
    configSetValue(key, buf);
}

void mgb_config_set_float(const char *key, float value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%g", (double)value);
    configSetValue(key, buf);
}

void mgb_config_set_enum(const char *key, int optIndex) {
    const Setting *s = settingsFind(key);
    if (!s || optIndex < 0 || optIndex >= s->enum_count) return;
    const char *token = s->enum_options[optIndex].token;
    if (token) configSetValue(key, token);
}

void mgb_config_set_string(const char *key, const char *value) {
    configSetValue(key, value ? value : "");
}

void mgb_config_reset_default(const char *key) {
    const Setting *s = settingsFind(key);
    if (!s) return;
    char buf[32];
    switch (s->type) {
        case SETTING_TYPE_INT:  snprintf(buf, sizeof(buf), "%d", s->def.s32_value); break;
        case SETTING_TYPE_UINT: snprintf(buf, sizeof(buf), "%u", s->def.u32_value); break;
        case SETTING_TYPE_FLOAT: snprintf(buf, sizeof(buf), "%g", (double)s->def.f32_value); break;
        case SETTING_TYPE_ENUM:
            configSetValue(key, settingsEnumTokenForValue(s, s->def.s32_value));
            return;
        default:
            if (s->def_string) configSetValue(key, s->def_string);
            return;
    }
    configSetValue(key, buf);
}
