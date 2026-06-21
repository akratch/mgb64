/**
 * config_pc.h — INI config file system for the GE007 PC port.
 *
 * Modeled after the Perfect Dark port's declarative config system.
 * Settings are registered with type + min/max, loaded from ge007.ini,
 * and saved back on shutdown or settings change.
 *
 * Usage:
 *   static s32 g_fullscreen = 0;
 *   configRegisterInt("Video.Fullscreen", &g_fullscreen, 0, 1);
 *   configInit();  // loads ge007.ini, applies values
 *   configSave();  // writes current values to ge007.ini
 */
#ifndef _PLATFORM_CONFIG_PC_H_
#define _PLATFORM_CONFIG_PC_H_

#include <ultra64.h>

#define CONFIG_FILENAME  "ge007.ini"
#define CONFIG_MAX_SETTINGS 128
#define CONFIG_MAX_KEYNAME  128
#define CONFIG_MAX_SECNAME   64

/* Register a setting. Call before configInit().
 * key format: "Section.Key" (e.g., "Video.Fullscreen").
 * min/max: validation range. Set min >= max to disable clamping. */
void configRegisterInt(const char *key, s32 *var, s32 min, s32 max);
void configRegisterFloat(const char *key, f32 *var, f32 min, f32 max);
void configRegisterUInt(const char *key, u32 *var, u32 min, u32 max);

/* Load settings from ge007.ini (in savedir). Call after all registrations. */
void configInit(void);

/* Save current values to ge007.ini. Returns 1 on success. */
s32 configSave(void);

/* Set a registered setting from a string value. Returns 1 if the key exists. */
s32 configSetValue(const char *key, const char *value);

/* Find a registered setting by "Section.Key" name.
 * Returns the backing variable pointer, or NULL if not found.
 * type_out receives: 0=int, 1=float, 2=uint  (or -1 if not found) */
void *configFindEntry(const char *key, int *type_out);

#endif /* _PLATFORM_CONFIG_PC_H_ */
