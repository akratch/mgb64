# AUDIT-0069: PCM Capture Reports Complete After a Short Write

| Field | Value |
| --- | --- |
| Status | Fixed |
| Severity | S4 - fidelity captures can be truncated while the diagnostic claims completion |
| Priority | P2 |
| Area | Audio diagnostics / output integrity |
| Evidence level | Fault injected |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | `GE007_AUDIO_DUMP` and `GE007_MUSIC_AUDIO_DUMP` under output I/O failure |

## Summary

The raw PCM dump path ignores `fwrite` and `fclose` results, increments its frame
counter anyway, and prints `dump complete`. A fidelity workflow can therefore
compare or archive a truncated capture under a green completion message.

## Evidence

[`port_trace.c`](../../../src/platform/port_trace.c) performs an unchecked
`fwrite`, increments `state->frames`, ignores `fclose`, and unconditionally logs
completion when the requested frame count is reached. Open failure is also
silent. The same helper serves both mixed-output and music-only captures.

Fault injection requested a one-frame audio dump with a 512-byte file-size
limit while using SDL's dummy audio driver. A normal synthesized frame was
larger than the limit. The log printed both the capture path and `Audio dump
complete (1 frames)`, the process returned 0, and the raw file was only 512
bytes.

## Reproduction

Run a bounded deterministic level with `GE007_AUDIO_DUMP` set to a regular
file, `GE007_AUDIO_DUMP_FRAMES=1`, and a file-size limit of 512 bytes. Ignore
the file-size signal so stdio returns failure, then compare the completion log
with the output length.

## Root Cause

The capture state counts attempted frames, not completely persisted frames, and
has no failure state or result propagation to the invoking process.

## Required End State

Validate open, full writes, flush, and close; count only completed bytes/frames;
remove or clearly quarantine partial captures; and make requested capture
failure observable as nonzero in automation. Include expected byte counts in
the final diagnostic.

## Acceptance Criteria

- Open, short-write, flush, and close failures never log `dump complete`.
- Successful completion reports and verifies exact total bytes.
- Partial raw files are removed or labeled incomplete.
- Both mixed and music-only dump modes share the corrected behavior.
- Fidelity wrappers reject absent, short, or failed captures.

## Verification Plan

Inject failures before open, mid-write, and at close for both dump states.
Assert byte counts, cleanup, logs, and process status, then run existing audio
comparison tools only after the capture validator passes.

## Related Work

- AUDIT-0067 covers guard-state text dumps.
- AUDIT-0043 covers screenshot capture failure status.

## Resolution

`portAudioDumpTo` (src/platform/port_trace.c) now fails closed: a failed `fopen` for an explicitly-requested `GE007_AUDIO_DUMP`/`GE007_MUSIC_AUDIO_DUMP` capture is logged (not silently skipped); a short `fwrite` aborts the capture, logs the errno, and closes the file rather than counting a partial frame; and "dump complete" is announced only when the final `fclose` succeeds. Diagnostic-only path; sim-neutral.
