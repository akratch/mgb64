// diag_log.cpp — see diag_log.h.
#include "diag_log.h"

#include <SDL.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

namespace {
std::mutex  g_mtx;
std::string g_ring;
const size_t kRingCap = 256 * 1024;
std::string g_logPath;
std::FILE  *g_logFile = nullptr;
bool        g_installed = false;

void appendRing(const char *data, size_t n) {
    std::lock_guard<std::mutex> lk(g_mtx);
    g_ring.append(data, n);
    if (g_ring.size() > kRingCap) g_ring.erase(0, g_ring.size() - kRingCap);
}

std::string prefsDir() {
    char *p = SDL_GetPrefPath("MGB64", "MGB64");
    std::string d = p ? p : "";
    if (p) SDL_free(p);
    return d;
}
}  // namespace

#if defined(_WIN32)

void DiagLog_install() { /* Windows tee wired in Part 3 */ }
int DiagLog_snapshot(char *buf, int cap) { if (buf && cap) buf[0] = '\0'; return 0; }
const char *DiagLog_path() { return ""; }

#else

#include <unistd.h>

namespace {
int g_realErr = -1;

void readerLoop(int rfd) {
    char buf[4096];
    ssize_t n;
    while ((n = read(rfd, buf, sizeof(buf))) > 0) {
        appendRing(buf, (size_t)n);
        if (g_logFile) {
            std::fwrite(buf, 1, (size_t)n, g_logFile);
            std::fflush(g_logFile);
        }
        if (g_realErr >= 0) {
            ssize_t w = write(g_realErr, buf, (size_t)n);  // write through to console
            (void)w;
        }
    }
}
}  // namespace

void DiagLog_install() {
    if (g_installed) return;
    g_installed = true;

    std::string dir = prefsDir();
    g_logPath = dir + "mgb64.log";
    std::string prev = dir + "mgb64.prev.log";
    std::rename(g_logPath.c_str(), prev.c_str());  // rotate previous run (ok if absent)
    g_logFile = std::fopen(g_logPath.c_str(), "w");

    g_realErr = dup(2);  // keep the real console for write-through
    int pfd[2];
    if (pipe(pfd) != 0) return;
    dup2(pfd[1], 1);  // stdout -> pipe
    dup2(pfd[1], 2);  // stderr -> pipe
    close(pfd[1]);
    // Line-buffer stdout so each line reaches the pipe promptly (a pipe is
    // block-buffered by default, which would swallow stdout on a quick exit).
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);
    std::thread(readerLoop, pfd[0]).detach();
}

int DiagLog_snapshot(char *buf, int cap) {
    if (!buf || cap <= 0) return 0;
    std::lock_guard<std::mutex> lk(g_mtx);
    int n = (int)g_ring.size();
    const char *src = g_ring.c_str();
    if (n >= cap) {
        src += (n - (cap - 1));
        n = cap - 1;
    }
    std::memcpy(buf, src, (size_t)n);
    buf[n] = '\0';
    return n;
}

const char *DiagLog_path() { return g_logPath.c_str(); }

#endif  // _WIN32
