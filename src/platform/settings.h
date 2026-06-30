#ifndef _PLATFORM_SETTINGS_H_
#define _PLATFORM_SETTINGS_H_

#include <stdio.h>
#include "config_pc.h"

typedef enum SettingType {
    SETTING_TYPE_INT,
    SETTING_TYPE_UINT,
    SETTING_TYPE_FLOAT,
    SETTING_TYPE_ENUM,
    SETTING_TYPE_STRING
} SettingType;

typedef enum SettingScope {
    SETTING_SCOPE_LIVE,
    SETTING_SCOPE_RESTART
} SettingScope;

typedef enum SettingOverrideSource {
    SETTING_OVERRIDE_NONE,
    SETTING_OVERRIDE_ENV,
    SETTING_OVERRIDE_CLI,
    SETTING_OVERRIDE_FAITHFUL
} SettingOverrideSource;

typedef union SettingValue {
    s32 s32_value;
    u32 u32_value;
    f32 f32_value;
} SettingValue;

typedef struct Setting {
    const char *key;
    SettingType type;
    SettingScope scope;
    void *ptr;
    SettingValue def;
    SettingValue min;
    SettingValue max;
    const char *def_string;
    const ConfigEnumOption *enum_options;
    s32 enum_count;
    size_t string_capacity;
    const char *env;
    const char *cli;
    const char *label;
    const char *help;
    SettingOverrideSource override_source;
} Setting;

void settingsRegisterInt(const char *key, s32 *var, s32 def, s32 min, s32 max,
                         SettingScope scope, const char *env, const char *cli,
                         const char *label, const char *help);
void settingsRegisterUInt(const char *key, u32 *var, u32 def, u32 min, u32 max,
                          SettingScope scope, const char *env, const char *cli,
                          const char *label, const char *help);
void settingsRegisterFloat(const char *key, f32 *var, f32 def, f32 min, f32 max,
                           SettingScope scope, const char *env, const char *cli,
                           const char *label, const char *help);
void settingsRegisterEnum(const char *key, s32 *var, s32 def,
                          const ConfigEnumOption *options, s32 option_count,
                          SettingScope scope, const char *env, const char *cli,
                          const char *label, const char *help);
void settingsRegisterString(const char *key, char *var, size_t capacity,
                            const char *def,
                            SettingScope scope, const char *env, const char *cli,
                            const char *label, const char *help);

s32 settingsCount(void);
const Setting *settingsAt(s32 index);
const Setting *settingsFind(const char *key);

const char *settingsTypeName(SettingType type);
const char *settingsScopeName(SettingScope scope);
const char *settingsOverrideSourceName(SettingOverrideSource source);
const char *settingsEnumTokenForValue(const Setting *setting, s32 value);
void settingsFormatEnumOptions(const Setting *setting, char *out, size_t out_size);
void settingsMarkCliOverride(const char *key);
/* Force one setting to its faithful (pre-remaster) value for this launch only.
 * Applies via configSetValue (validated/clamped) and marks the setting as a
 * transient SETTING_OVERRIDE_FAITHFUL override so configSave never persists it
 * -- a `--faithful` run leaves the user's saved ge007.ini untouched. Returns 1
 * if the value was applied. Used by platformApplyFaithfulPreset(). */
s32 settingsApplyFaithfulValue(const char *key, const char *value);
void settingsApplyEnvOverrides(void);
void settingsResetAllToDefaults(void);
void settingsPrintList(FILE *f);
void settingsPrintDump(FILE *f);

#endif /* _PLATFORM_SETTINGS_H_ */
