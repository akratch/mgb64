/**
 * ge_debug.h -- Centralized diagnostic macros for the NATIVE_PORT.
 *
 * Provides:
 *   GEDBG(fmt, ...)       -- conditional stderr logging (GE007_DEBUG=1)
 *   GEABORT_IF(cond, ...) -- conditional abort (GE007_ASSERT_ON_FAIL=1)
 *   ge_dbg_enabled()      -- cached check for GE007_DEBUG env var
 *
 * Coexists with the existing GE007_VERBOSE printf pattern.
 * Include only inside #ifdef NATIVE_PORT blocks.
 */
#ifndef GE_DEBUG_H
#define GE_DEBUG_H

#ifdef NATIVE_PORT

#include <stdio.h>
#include <stdlib.h>

static inline int ge_dbg_enabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char *env = getenv("GE007_DEBUG");
        cached = (env != NULL) ? 1 : 0;
    }
    return cached;
}

static inline int ge_dbg_assert_on_fail(void) {
    static int cached = -1;
    if (cached < 0) {
        const char *env = getenv("GE007_ASSERT_ON_FAIL");
        cached = (env != NULL) ? 1 : 0;
    }
    return cached;
}

#define GEDBG(fmt, ...) do { \
    if (ge_dbg_enabled()) { \
        fprintf(stderr, "[GEDBG] " fmt "\n", ##__VA_ARGS__); \
        fflush(stderr); \
    } \
} while (0)

#define GEABORT_IF(cond, fmt, ...) do { \
    if ((cond)) { \
        fprintf(stderr, "[GEASSERT] " fmt "\n", ##__VA_ARGS__); \
        fflush(stderr); \
        if (ge_dbg_assert_on_fail()) abort(); \
    } \
} while (0)

#else /* !NATIVE_PORT */

static int ge_dbg_enabled(void) {
    return 0;
}

static int ge_dbg_assert_on_fail(void) {
    return 0;
}

static void ge_dbg_noop(const char *fmt, ...) {
    (void)fmt;
}

static void ge_dbg_abort_noop(int cond, const char *fmt, ...) {
    (void)cond;
    (void)fmt;
}

#define GEDBG if (0) ge_dbg_noop
#define GEABORT_IF if (0) ge_dbg_abort_noop

#endif /* NATIVE_PORT */

#endif /* GE_DEBUG_H */
