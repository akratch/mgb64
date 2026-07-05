// host_window.c — see host_window.h.
#include "host_window.h"

#include <stddef.h>

static void *g_hostWindow = NULL;
static void *g_hostGL     = NULL;
static int   g_hasHost    = 0;

void platformSetHostWindow(void *sdl_window, void *gl_context) {
    g_hostWindow = sdl_window;
    g_hostGL     = gl_context;
    g_hasHost    = (sdl_window != NULL);
}

int platformHasHostWindow(void) { return g_hasHost; }

void *platformHostWindow(void) { return g_hostWindow; }

void *platformHostGLContext(void) { return g_hostGL; }
