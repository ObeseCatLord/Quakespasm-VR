# Full Windows Opus dependency results

2026-10-02. The native9649 Release build reaches the linker and identifies the
four missing Opus encoder imports. The existing main-reference DLL is identical
to the decoder-only bundle. This repairs the existing codec boundary; no voice
protocol, music owner, loader or second codec is introduced.

The official Opus1.5.2 archive matches its published SHA256. Stock configure and
make both exit0 with the pinned MSVCRT flags. The Luna rebuild script independently
runs successfully with those same options. PE x64 exports preserve all23 prior
symbols and contain80 exports, including the four required encoder imports.
Runtime dependencies remain exactly KERNEL32.dll/msvcrt.dll. Both GNU and native
MSVC import libraries describe the same actual full DLL exports. Shipped source,
compiler/options, product hashes and upstream BSD notice are recorded in
[dependency provenance](../Windows/codecs/opus-build.json); the original DLL hash
is retained there. Existing Opus/Opusfile public-header ABI and the Opusfile/Ogg
binaries are unchanged.

Native MSVC14.44 /W4 /WX /std:c11 /MT smoke compilation and native CPU-only run
both return0. Generated non-silent20ms mono48k PCM is actually encoded, decoded
into960 non-silent frames, reset and destroyed. Existing bundled Opusfile/Ogg
actually decode a privately generated non-silent .opus file through EOF using
the new DLL. The runtime CHECK macro stays active with NDEBUG. No engine/GPU,
audio device, physical microphone, headset or human listening test runs.

An initial smoke launcher incorrectly combined global /TC with import-library
arguments, treating libraries as C sources. Its failed receipts are retained;
corrected /link separation passes without source or warning-setting changes.
A default cross-compiler CRT trial was rejected because it selected UCRT;
the proven explicit MSVCRT build is the only integrated product.

The actual local build/export/import/smoke receipts are retained privately in
FastGames/qsvr-opus-windows-t0op4g1f. This proves the bounded shared-library voice
and music ABI, not complete game builds or Windows gameplay. Fresh Release/Debug
and complete PE dependency checks remain the immediate next acceptance boundary.
The existing postbuild, CI archive/upload and installer now carry the exact BSD
notice. Installer execution/distribution is not claimed.
