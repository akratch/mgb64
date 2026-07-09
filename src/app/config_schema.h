// config_schema.h — clean, engine-free view of the settings registry for the
// app's auto-generated settings UI.
//
// Implemented in src/platform/config_schema.c (compiled into the engine), which
// translates the engine's Setting registry (settings.h) into these plain-type
// entries and wraps configSetValue()/configInit()/configSave(). The app never
// includes engine headers (which carry the math.h shim + ultra64 types).
#ifndef MGB64_CONFIG_SCHEMA_H
#define MGB64_CONFIG_SCHEMA_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MGB_CFG_INT,
    MGB_CFG_UINT,
    MGB_CFG_FLOAT,
    MGB_CFG_ENUM,
    MGB_CFG_STRING
} MgbCfgKind;

typedef struct {
    char section[32];   // "Video" | "Input" | "Game" | "Audio"
    char name[64];      // key within the section
    char key[96];       // full "Section.name" (as configSetValue expects)
    char label[80];     // human label (falls back to name)
    char help[224];     // one-line description
    MgbCfgKind kind;
    int   is_live;      // 1 = applies next frame; 0 = needs restart
    float min_val, max_val, def_val;
    int   cur_int;      // current value for INT/UINT
    float cur_float;    // current value for FLOAT
    int   enum_count;   // options for ENUM
    int   cur_enum_index;
    int   advanced;     // 1 = dev/diagnostic (hide behind "Advanced" disclosure)
} MgbCfgEntry;

// Lifecycle. Safe to call once at app startup; the engine boot re-registers
// idempotently (settingsFindOrAdd) and reloads ge007.ini.
void mgb_config_init(void);      // savedir + register + load ge007.ini
int  mgb_config_save(void);      // write ge007.ini

// Enumeration.
int  mgb_config_count(void);
int  mgb_config_get(int index, MgbCfgEntry *out);  // 1 on success
const char *mgb_config_enum_token(const char *key, int optIndex);

// Current INT/UINT/ENUM value for a key, or `fallback` if the key is absent or
// not an integer-typed setting. Lightweight lookup for app code that needs one
// value (e.g. the launcher's Game.CheckForUpdates gate) without enumerating.
int  mgb_config_get_int(const char *key, int fallback);

// Current STRING value for a key. Copies into out (always NUL-terminated when
// out_size > 0). Returns 1 if the key exists and is a string setting, else 0
// (and out is set to ""). Lightweight lookup for the settings UI, which needs
// the string value without growing MgbCfgEntry.
int  mgb_config_get_string(const char *key, char *out, int out_size);

// Mutation (validated/clamped by the engine).
void mgb_config_set_int(const char *key, int value);
void mgb_config_set_float(const char *key, float value);
void mgb_config_set_enum(const char *key, int optIndex);
void mgb_config_set_string(const char *key, const char *value);
void mgb_config_reset_default(const char *key);

#ifdef __cplusplus
}
#endif

#endif  // MGB64_CONFIG_SCHEMA_H
