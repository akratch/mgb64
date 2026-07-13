# AUDIT-0043: Auto-Screenshot Write Failure Still Exits Successfully

| Field | Value |
| --- | --- |
| Status | Fixed |
| Severity | S3 - capture automation reports success without producing its artifact |
| Priority | P1 |
| Area | Screenshot automation / process status |
| Evidence level | Fault injected |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | `--screenshot-exit` when BMP creation or writing fails |

## Resolution

Fixed (already resolved by the pre-release hardening) — platformSaveScreenshot tracks write_ok, s_lastScreenshotFailed is pessimistic-set and cleared only on full success, and platformFinishAutoScreenshotIfRequested exit(4)s on failure (verified 2026-07-13).

## Summary

The screenshot writer logs open and I/O failures but returns no status. The
auto-screenshot completion path always prints `complete` and exits zero unless
a renderer crash recovery occurred. A capture run can therefore succeed at the
process level while producing no screenshot.

## Evidence

[`platform_sdl.c`](../../../src/platform/platform_sdl.c) implements
`platformSaveScreenshot` as `void`. It returns early when `fopen` or allocation
fails and removes a truncated output after later I/O failure, but cannot inform
its caller. `platformFinishAutoScreenshotIfRequested` then prints
`Auto-screenshot complete` and calls `exit(0)` whenever crash recovery count is
zero.

Fault injection ran a valid Dam screenshot-exit session from a read-only working
directory. The log contained:

```text
[SDL] Failed to save screenshot screenshot_000.bmp: Permission denied
[GE007-PC] Auto-screenshot complete, exiting.
```

No BMP existed and the process exit status was 0.

## Reproduction

Run a valid ROM-backed screenshot-exit command from a directory that cannot
create files while placing `--savedir` in a separate writable directory:

```sh
ge007 --rom ROM --savedir WRITABLE --level dam --deterministic \
  --screenshot-frame 1 --screenshot-exit
echo "$?"
```

The current result is zero after the permission-denied capture.

## Root Cause

Screenshot generation was designed as a manual best-effort action. Automation
later reused the void API without adding an artifact-success contract.

## Required End State

Return a structured capture result through allocation, backend readback, open,
write, close, and finalization. `--screenshot-exit` must exit nonzero and never
print complete unless the final expected artifact exists and closed cleanly.
Manual capture should retain gameplay but visibly/logically report failure.

## Acceptance Criteria

- Open, allocation, renderer readback, short-write, and close failures make
  screenshot-exit nonzero.
- A successful capture still exits zero and produces a nonempty valid BMP.
- The completion line is emitted only after successful close.
- A failed capture leaves no truncated BMP.
- Labeled, numbered, menu, display-cast, and gameplay-timer captures share the
  same result contract.
- Harnesses assert both status and artifact validity.

## Verification Plan

Add capture fault injection for every failure stage plus a successful control.
Run the command-level test on OpenGL and Metal and the source-validity test on
GLES, checking exit code, log wording, file presence, BMP header, and size.

## Related Work

- AUDIT-0040 covers stale framebuffer content on GLES after a capture succeeds.
- AUDIT-0035 covers the app shell independently discarding engine failure codes.
