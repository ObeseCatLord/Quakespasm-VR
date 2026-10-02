# Full Windows Opus dependency plan

2026-10-02, before replacement. Native9649 Release compiles every engine/shader
owner but link fails on opus_encode/encoder_ctl/create/destroy. objdump confirms
bundled DLL exports only decoder API. Main reference DLL is byte-identical SHA256
57f00fd04e7b62fc26a24b7d5af8ed35532ce0a92da3a6aab565714f853be131;
no working full encoder binary to reuse. Existing libopusfile imports named decoder
symbols from libopus-0.dll (not ordinals), and engine's C ABI/header usage fits
full library. Preserve actual voice/Opusfile/music owners/protocol, no second codec.

Replace decoder-only x64 libopus-0.dll, MSVC libopus.lib and MinGW libopus.dll.a
with one full official Opus1.5.2 build. Official source SHA256
65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1
from https://opus-codec.org/release/stable/2024/04/12/libopus-1_5_2.html.
Use version-matched stock configure build/cross MinGW toolchain already installed,
shared full encoder+decoder, preserve libopus-0.dll name and existing msvcrt/kernel
ABI/runtime boundaries. Disable extra programs/docs/static library; -static-libgcc
avoids adding a compiler-runtime DLL. Stock CPU dispatch/optimizations retained.
Official1.5.2 fixes Windows AVX2 alignment; optional neural/DRED feature goals are
not introduced. Existing public header subset remains ABI-compatible; no duplicate
static voice codec nor alternate encoder/protocol/loader.

Build in fresh private FastGames folder from verified source; record tool versions,
exact configure/make arguments and product hashes. Export table must include all
old DLL symbols plus four missing encoder imports; retain original named-decoder
Opusfile requirements. GNU import archive from actual build; generate native MSVC
import library from actual export names through existing lib.exe. No DLL ordinal
assumptions/new global installations/registry/driver settings. Inspect PE x64 and
actual runtime dependencies. Copy only matching validated products into2.0,
retain BSD COPYING/license and source hash/provenance/rebuild instructions. Update
existing postbuild/archive/installer boundaries to include that notice where needed.

Add a reproducible script using exact proven upstream build commands, not another
codec implementation. Main owns binary integration/project/packaging/main conclusion;
Luna may implement the bounded script once commands are verified. Native CPU-only
encoder+decoder20ms smoke (generated signal, no mic/device/GPU/game launch) and
existing music decode ABI acceptance may be used to verify dependency coherence.
Then fresh immutable source archive/full Release+Debug compile/shader/link/PE
inspection and all imports closure. Linux/ARM actual code owners unchanged by
Windows binary dependency; final artifact reconciliation still required.

Proven compiler defaults use UCRT on this host. Explicit
CFLAGS=-O2 -g0 -mcrtdll=msvcrt-os and
LDFLAGS=-static-libgcc -mcrtdll=msvcrt-os retain the original two system DLLs.
The default-CRT trial is retained privately and is not shipped. The Luna rebuild
script completes successfully with the proven flags. Native MSVC CPU-only voice
encode/decode/reset and existing Opusfile decode-to-EOF both return0.
