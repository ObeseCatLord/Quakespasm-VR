# Windows Opus dependency

The bundled x64 Opus 1.5.2 DLL supplies both voice encoding and music/voice
decoding through the existing shared-library boundary. The previous bundle
only exported decoders. The bundled Opusfile DLL remains unchanged and imports
named decoder symbols preserved by this replacement. The existing public Opus
headers use the compatible opaque C API; no second voice codec is linked.

Source: https://downloads.xiph.org/releases/opus/opus-1.5.2.tar.gz
Official release/hash: https://opus-codec.org/release/stable/2024/04/12/libopus-1_5_2.html
License: `LICENSE.opus.txt`, copied verbatim from upstream COPYING.
`opus-build.json` records source, compiler, options and actual shipped hashes.

To rebuild, obtain the official source archive and run from the repository root:

```sh
Windows/codecs/build-opus.sh /path/to/opus-1.5.2.tar.gz /new/private/output
```

The script verifies the pinned hash, refuses an existing output directory and
uses the installed MinGW x64 cross compiler. Copy its two DLL/import products
into `Windows/codecs/x64`, and upstream COPYING to `LICENSE.opus.txt` only after
checking the export table and runtime dependencies. `-mcrtdll=msvcrt-os` retains
the original system MSVCRT boundary and `-static-libgcc` avoids a new compiler
runtime DLL. Stock CPU dispatch remains enabled; DRED/OSCE are disabled.

From an installed x64 MSVC developer prompt, generate the matching MSVC import
library using the reviewed full export list:

```bat
lib /def:Windows\codecs\libopus.def /machine:x64 /out:Windows\codecs\x64\libopus.lib /nologo
```

If a future version changes exports, regenerate and review the DEF from that
actual DLL before creating the library; do not reuse stale import archives.
PE inspection must preserve all old exports and the engine's encoder imports,
with x64 architecture and only KERNEL32.dll/msvcrt.dll dependencies.

`tests/windows_codec_smoke.c` is a CPU-only native acceptance program. Compile
with `/W4 /WX /std:c11 /MT`, the bundled codec include path, and link the matching
`libopus.lib` plus existing `libopusfile.lib`. Place the tested DLL alongside the
existing Opusfile/Ogg DLLs. Pass an owned non-silent `.opus` file: the program
requires a real 20ms voice encode/decode/reset and music decode to EOF. It does
not launch the engine, GPU or audio devices. Native compilation/run and the
full game Release/Debug builds are separate acceptance boundaries.
