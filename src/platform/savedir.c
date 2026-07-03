/**
 * savedir.c — Smart save directory detection for the GE007 PC port.
 *
 * Priority:
 *   1. --savedir <path> command-line override
 *   2. CWD if ge007.ini or ge007_eeprom.bin already exists there (portable mode)
 *   3. CWD if writable (first launch in project dir)
 *   4. $HOME/.ge007/ (created on demand)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SAVEDIR_MAX_PATH 1024
#define SAVEDIR_NAME     ".ge007"

static char s_saveDir[SAVEDIR_MAX_PATH];
static char s_pathBuf[SAVEDIR_MAX_PATH];
static int  s_initialized;

static int dir_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

static int file_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

static int dir_writable(const char *path) {
    return (access(path, W_OK) == 0);
}

static int ensure_dir(const char *path) {
    if (dir_exists(path)) return 1;
    return (mkdir(path, 0755) == 0);
}

void savedirInit(const char *savedir_override)
{
    if (s_initialized) return;
    s_initialized = 1;

    /* Priority 1: explicit override */
    if (savedir_override && savedir_override[0]) {
        snprintf(s_saveDir, SAVEDIR_MAX_PATH, "%s", savedir_override);
        /* Ensure trailing slash */
        size_t len = strlen(s_saveDir);
        if (len > 0 && s_saveDir[len - 1] != '/') {
            if (len + 1 < SAVEDIR_MAX_PATH) {
                s_saveDir[len] = '/';
                s_saveDir[len + 1] = '\0';
            }
        }
        ensure_dir(s_saveDir);
        printf("[SAVEDIR] Using override: %s\n", s_saveDir);
        return;
    }

    /* Priority 2: CWD if save files already exist (portable mode) */
    if (file_exists("ge007_eeprom.bin") || file_exists("ge007.ini")) {
        s_saveDir[0] = '\0'; /* empty = CWD-relative */
        printf("[SAVEDIR] Using CWD (existing save files found)\n");
        return;
    }

    /* Priority 3: CWD if writable (first launch from project directory) */
    if (dir_writable(".")) {
        s_saveDir[0] = '\0'; /* empty = CWD-relative */
        printf("[SAVEDIR] Using CWD\n");
        return;
    }

    /* Priority 4: $HOME/.ge007/ */
    {
        const char *home = getenv("HOME");
        if (!home) home = getenv("USERPROFILE"); /* Windows fallback */
        if (home) {
            snprintf(s_saveDir, SAVEDIR_MAX_PATH, "%s/%s/", home, SAVEDIR_NAME);
            if (ensure_dir(s_saveDir)) {
                printf("[SAVEDIR] Using %s\n", s_saveDir);
                return;
            }
        }
    }

    /* Fallback: CWD (may fail on writes, but best we can do) */
    s_saveDir[0] = '\0';
    printf("[SAVEDIR] Falling back to CWD\n");
}

const char *savedirPath(const char *filename)
{
    if (!s_initialized) {
        savedirInit(NULL);
    }

    if (s_saveDir[0]) {
        snprintf(s_pathBuf, SAVEDIR_MAX_PATH, "%s%s", s_saveDir, filename);
    } else {
        snprintf(s_pathBuf, SAVEDIR_MAX_PATH, "%s", filename);
    }
    return s_pathBuf;
}

const char *savedirGet(void)
{
    if (!s_initialized) {
        savedirInit(NULL);
    }
    return s_saveDir;
}
