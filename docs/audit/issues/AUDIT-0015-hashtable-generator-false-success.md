# AUDIT-0015: Hashtable Generator Emits Invalid Output and Exits Zero on Failure

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S3 - matching/checksum evidence can be silently invalid |
| Priority | P2 |
| Area | Matching build / checksum tooling |
| Evidence level | Fault injected and source proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | macOS system Bash, missing build objects, failed section extraction, and direct use without `-o` |

## Summary

The object-section hashtable generator continues after argument-parsing,
input-glob, extraction, and checksum failures, then exits zero. On macOS's
system Bash 3.2 its version parser uses unsupported Bash 4 lowercase expansion,
so a valid `-v u` argument leaves the country code empty. The script proceeds
over literal unmatched globs and creates a misnamed CSV that can be mistaken for
valid matching evidence.

Even on a newer Bash, omitting the documented optional `-o` value produces
`full_hashtable_.csv` because the selected version is stored in `ARG_VERSION`
but the filename references undefined `version`.

## Evidence

[`build_hashtable.sh`](../../../scripts/make/build_hashtable.sh#L18) parses the
version with `${OPTARG,,}` at lines 21-31. The script's shebang is `/bin/bash`;
the macOS-provided Bash 3.2 does not implement that case-conversion expansion.
The resulting `bad substitution` does not terminate this script.

Line 35 stores the argument in `ARG_VERSION`, but line 54 formats the default
output from `${version}`, which is never assigned. The script then removes and
touches that bad output name before proving that any input exists.

Every object loop uses a glob such as `build/${COUNTRY_CODE}/src/*.o` without
enabling `nullglob` or explicitly checking matches. With an empty/missing build,
the loop body receives the literal `*.o` pathname. Extraction and checksum
commands are not checked, and the script has no `set -e` or accumulated error
status. Its final successful `rm -f` controls the process exit.

An isolated fault fixture placed an executable `mips-linux-gnu-objcopy` shim on
`PATH` to pass the dependency check, then invoked the script with `-v u` and no
build tree or `-o`. The current macOS host produced:

```text
...build_hashtable.sh: line 21: ${OPTARG,,}: bad substitution
adding build//src/*.o
adding build//src/game/*.o
...
```

It created `full_hashtable_.csv` and exited zero.

## Reproduction

On macOS, use a disposable directory and a harmless executable shim so no MIPS
toolchain is required:

```sh
tmp="$(mktemp -d)"
ln -s /usr/bin/true "$tmp/mips-linux-gnu-objcopy"
(
  cd "$tmp"
  PATH="$tmp:$PATH" \
    /path/to/mgb64/scripts/make/build_hashtable.sh -v u
)
echo "$?"
find "$tmp" -maxdepth 1 -name 'full_hashtable*' -print
```

The invocation diagnoses a bad substitution and missing inputs in its command
output, but returns zero and creates `full_hashtable_.csv`.

On Bash 4 or newer, the independent filename bug is reproduced by supplying a
valid populated `build/u` tree and omitting `-o`; the output still lacks `u`.

## Root Cause

The script assumes Bash 4 syntax despite selecting the older system Bash by
absolute shebang. It also has no explicit success contract: version validation,
glob expansion, extraction, checksum generation, row count, and output naming
are not checked. A variable-name typo in the fallback filename completes a
plausible-looking but invalid output path.

## Required End State

Make the generator deterministic and fail closed:

- either use portable Bash 3 syntax or select/document a required newer Bash;
- normalize and validate `-v` into one canonical variable and reject every
  unsupported value;
- derive the default filename from that same variable;
- require at least one object in every required class, or explicitly define
  which classes may be empty;
- check every extraction and checksum command;
- write to a temporary file and publish atomically only after validation;
- validate each CSV row's digest, section, and source pathname; and
- exit nonzero without leaving a final output on any failure.

Do not iterate literal unmatched glob patterns. Use an array populated from a
checked enumeration, preserving spaces and special characters in paths.

## Acceptance Criteria

- `-v u`, `-v j`, and `-v e` work on every supported host and produce the
  correct country-specific object set.
- The documented no-`-o` form creates `full_hashtable_u.csv`,
  `full_hashtable_j.csv`, or `full_hashtable_e.csv` as appropriate.
- Unsupported or missing versions exit two before touching output.
- Missing object directories, an empty required class, extractor failure,
  checksum failure, and an unwritable destination each exit nonzero and leave
  no completed CSV.
- No emitted row refers to a literal wildcard or contains an empty digest.
- A valid fixture's row count and digests match an independently computed
  oracle.
- The wrapper scripts that pass explicit `-o` values remain compatible.
- ShellCheck and a macOS system-Bash execution test are clean for this script.

## Verification Plan

Build a small fixture tree with synthetic ELF objects containing the relevant
sections and run the generator for all three country codes. Compare output to a
reference implementation. Add negative fixtures for each required failure and
run them under macOS Bash 3.2 plus the supported Linux Bash. Retain the harmless
tool-shim test to prove a failed extraction can never produce a successful
final artifact.

## Related Work

- [`rebuild_allver_hashtables.sh`](../../../scripts/make/rebuild_allver_hashtables.sh)
  and the checksum templates pass explicit output paths, so they avoid only the
  filename typo; they do not repair the parser or false-success behavior.
- This is separate from
  [AUDIT-0014](AUDIT-0014-nuke-cleanup-quotes-asset-globs.md), which concerns
  stale inputs surviving the preceding cleanup step.
