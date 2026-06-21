#ifndef _PLATFORM_SETTINGS_H_
#define _PLATFORM_SETTINGS_H_

#include <stdio.h>
#include <ultra64.h>

typedef enum SettingType {
    SETTING_TYPE_INT,
    SETTING_TYPE_UINT,
    SETTING_TYPE_FLOAT
} SettingType;

typedef enum SettingScope {
    SETTING_SCOPE_LIVE,
    SETTING_SCOPE_RESTART
} SettingScope;

typedef enum SettingOverrideSource {
    SETTING_OVERRIDE_NONE,
    SETTING_OVERRIDE_ENV,
    SETTING_OVERRIDE_CLI
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

s32 settingsCount(void);
const Setting *settingsAt(s32 index);
const Setting *settingsFind(const char *key);

const char *settingsTypeName(SettingType type);
const char *settingsScopeName(SettingScope scope);
const char *settingsOverrideSourceName(SettingOverrideSource source);
void settingsMarkCliOverride(const char *key);
void settingsApplyEnvOverrides(void);
void settingsResetAllToDefaults(void);
void settingsPrintList(FILE *f);
void settingsPrintDump(FILE *f);

#endif /* _PLATFORM_SETTINGS_H_ */
