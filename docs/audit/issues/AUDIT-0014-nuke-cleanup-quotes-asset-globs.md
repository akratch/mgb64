# AUDIT-0014: Nuke Cleanup Quotes Asset Globs and Leaves Generated Binaries

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S4 - clean rebuilds can silently reuse stale generated assets |
| Priority | P2 |
| Area | Build tooling / cleanup |
| Evidence level | Runtime reproduced and source proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Legacy `make nuke` workflow on every shell platform |

## Summary

The `make nuke` helper passes all asset wildcard patterns to `rm` inside quotes.
The shell therefore never expands them. The script exits zero and deletes
fixed-name directories, but every intended generated `.bin` asset remains.

Developers can believe they performed a complete clean rebuild while stale
music, object-segment, setup, text, or ramrom binaries continue to influence the
next build.

## Evidence

[`clean_nuke.sh`](../../../scripts/make/clean_nuke.sh#L29) announces `deleting
assets`, then calls `rm -r -f` with patterns such as
`"assets/music/*.bin"` and `"assets/obseg/bg/*.bin"`. Quoting preserves the
literal asterisk, so `rm -f` silently ignores each nonexistent literal pathname.

The same invocation does successfully remove literal paths `bin/` and
`assets/images/split/` at lines 24-25. Its mixed behavior makes the false clean
plausible and gives no diagnostic.

An isolated temporary-tree control created both
`assets/music/probe.bin` and `assets/obseg/bg/probe.bin`, plus the two fixed-name
directories, then executed the repository script from that temporary working
directory. It exited zero and printed all three deletion messages. Both probe
files remained, while `bin/` and `assets/images/split/` were removed.

[`Makefile`](../../../Makefile#L397) invokes this helper for the documented
`nuke` target after ordinary `clean`.

## Reproduction

Run from an isolated directory to avoid touching the checkout:

```sh
tmp="$(mktemp -d)"
mkdir -p "$tmp/assets/music" "$tmp/assets/obseg/bg" \
  "$tmp/assets/images/split" "$tmp/bin"
touch "$tmp/assets/music/probe.bin" "$tmp/assets/obseg/bg/probe.bin"
(cd "$tmp" && /path/to/mgb64/scripts/make/clean_nuke.sh u build)
test -f "$tmp/assets/music/probe.bin"
test -f "$tmp/assets/obseg/bg/probe.bin"
```

The helper exits zero and both final tests succeed, proving that the files it
claimed to delete remain.

## Root Cause

The script quotes wildcard-bearing pathnames as though they were literal file
paths. Shell globs must be expanded separately from the quoted directory prefix,
or files must be enumerated with a structured primitive such as `find`.
`rm -f` masks the mistake because missing literal names are not errors.

## Required End State

Delete only the intended generated `.bin` files using a path-safe enumeration.
For example, enumerate the explicit allowed directories with `find ... -type f
-name '*.bin' -delete`, without following symlinks or crossing those directory
boundaries. Preserve source-controlled files and unrelated formats.

Make the helper independent of the caller's current directory by resolving the
repository root from the script path and changing to it with a checked failure.
Validate `BUILD_DIR_BASE` and country-code inputs before any recursive removal.
The command must report the count of removed generated files and verify that no
matching target remains.

## Acceptance Criteria

- A fixture containing one `.bin` in every listed asset directory is empty of
  those files after the helper runs.
- Fixed-name generated directories are still removed as intended.
- Non-`.bin` files, source-controlled inputs, sibling directories, and paths
  outside the repository remain untouched.
- Spaces and glob characters in the checkout's parent path do not change the
  deletion set.
- Symlinks in an asset directory cannot cause deletion outside the checkout.
- Missing directories are harmless, while malformed or empty destructive-path
  inputs fail before deletion.
- `make nuke` followed by a build regenerates required assets rather than
  reusing prior outputs.
- A shell test asserts both deletion and containment behavior.

## Verification Plan

Add a temporary-directory test that creates the full directory matrix, target
files, protected controls, and outward-pointing symlinks. Invoke the helper with
an explicit repository/root parameter or testable library function, then assert
the exact before/after file set. Run ShellCheck on the helper and execute one
real `make nuke` plus clean rebuild in a disposable checkout.

## Related Work

- ShellCheck flags the helper's word/array expansion but cannot directly infer
  the quoted-glob no-op. Retain a behavioral fixture even after lint is clean.
- This affects the legacy matching/decomp build path, not CMake's native-port
  object cleanup directly.
