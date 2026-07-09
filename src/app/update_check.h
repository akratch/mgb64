// update_check.h — quiet "a newer MGB64 is available" check for the launcher.
//
// At launcher startup (interactive path only) a background thread shells out to
// the system `curl` to read the latest GitHub release tag, compares it against
// the compiled-in version, and — if strictly newer and not previously dismissed
// — makes a tag available for a small dismissible banner. No library deps, no
// telemetry: a single plain GET, curl absent/offline/timeout => silently nothing.
#ifndef MGB64_UPDATE_CHECK_H
#define MGB64_UPDATE_CHECK_H

#include <stddef.h>

// Start the check on a background SDL thread. Runs at most once per process. A
// no-op (and spawns no subprocess) when: the invocation is an automation/
// deterministic one (mgb_is_automation_invocation), GE007_UPDATE_CHECK=0, or the
// Game.CheckForUpdates setting is off. argc/argv are the process args (for the
// automation gate). Safe to call before the window's event loop begins.
void UpdateCheck_start(int argc, char **argv);

// If a strictly-newer, not-yet-dismissed release tag is ready, copy it into
// out[cap] and return 1; otherwise return 0. Cheap to poll every frame.
int UpdateCheck_bannerTag(char *out, size_t cap);

// 1 once the check has finished (or was declined without spawning anything).
// Used by the headless self-test harness (MGB64_UPDATE_CHECK_SELFTEST).
int UpdateCheck_isDone(void);

// Persist `tag` as dismissed (app config) so its banner never returns.
void UpdateCheck_dismiss(const char *tag);

#endif  // MGB64_UPDATE_CHECK_H
