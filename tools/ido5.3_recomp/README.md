# ido5.3_recomp

A static recompilation of Silicon Graphics' **IDO 5.3** C compiler, used to
compile the decompiled game code with period-accurate codegen for N64
byte-matching (the same compiler the original game was built with).

- **Recompilation tooling upstream:** https://github.com/decompals/ido-static-recomp
  (MIT-licensed).
- **The IDO compiler itself** is © Silicon Graphics, Inc. and is proprietary. It
  is **not** redistributed here as a prebuilt binary; the recompiled sources are
  built locally as part of the toolchain.

Building the recompiled compiler executables requires local, legally obtained
IDO/IRIX compiler input files under `tools/irix/root` in the main repository
(for example `tools/irix/root/usr/bin/cc` and
`tools/irix/root/usr/lib/err.english.cc`). That ignored tree is not part of the
public checkout and must not be committed or redistributed. If those files are
absent, the N64 matching target will stop with a preflight error; the native
CMake port does not need them.

This component is **not** original to MGB64 and is **not** covered by this
project's MIT license. See [../../THIRD_PARTY.md](../../THIRD_PARTY.md).
