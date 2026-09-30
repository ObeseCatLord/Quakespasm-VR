# Separate rerelease English source on native localization

2026-09-30. Stage 2 of `localization-message-sources-2.0-plan.md`.
Solo maintainer; production ownership is `Quake/common.c` only. No game-data
mount, second dictionary, content-root service, renderer or language-policy
replacement. Main owns documentation; one web worker owns the production file.
User-owned `migration-2.0.md` remains untouched. No executable checks until full
implementation; Linux/ARM qualification at the end, Windows and live tests later.

## Verified brief

| Fact and source | Consequence |
| --- | --- |
| [verified: source] Inherited `quakespasm-openvr/Quake/common.c:3327–3338` selects explicit `-rerelease`, otherwise sibling, otherwise resolved Steam rerelease. `-norerelease` suppresses automatic selection; `-nosteam` suppresses Steam discovery. | Copy this precedence, including explicit missing-root suppression of automatic fallback. Do not turn localization into an enhanced-model opt-in. |
| [verified: source] Primary `COM_LoadRereleaseLocalization` at 2455 reads only `localization/loc_english.txt`, validates bounded PACK directory and entry lengths, returns optional failure, and never mounts the pack. | Reuse its reader, not its adjacent content-root/add-on/model services. |
| [verified: source] Native `COM_InitSteamAPI` at 4051 already resolves Steam even for valid portable basedirs, then restricts API activation to the selected Steam installation. It has one caller. | Use this existing resolution for optional localization before the API prefix check. No second Steam discovery call is needed. |
| [verified: source] Native `COM_ValidatePackDirectoryEntries` at 2390 validates names and every entry using subtraction-based 64-bit bounds; PACK directory limit is 2048 entries. Native `Sys_FileSeek` reports success as zero and `Sys_FileOpenRead` returns `qfilesize_t`. | Reuse the validator and limit; adapt primary integer file size and unchecked seeks to native contracts. Malformed optional input must remain nonfatal. |
| [verified: source] Native `Mem_AllocNonZero`/`Mem_Free` in `Quake/mem.c` share the native allocator and allocation may return NULL. | Use matching native allocation/free and explicit NULL handling. Do not mix primary libc allocations with native localization cleanup. |
| [verified: source] Native `LOC_LoadFile` at 4680 first calls `COM_LoadFile`, then tries native direct-root loose files before KPF. Native language selection, parsing, UTF8 and reload/shutdown already own the returned text. | Preserve those loose-file overrides: add an exact-English optional read after direct-root opens fail, before KPF. Keep all other loading and language policy. |

## Decision and alternatives

Lean: retain one static pack filename under the existing filesystem owner.
At filesystem initialization, select explicit or sibling source and return a
frame-local boolean indicating whether Steam fallback is still eligible.
Pass that boolean to the sole `COM_InitSteamAPI` caller; after its successful
resolution, select `<Steam root>/rerelease/id1/pak0.pak` before its unchanged
achievement/API prefix check. Roots use bounded formatting and file-type checks.

Reject a new store-discovery loop (duplicates native discovery), a global
automatic-policy state machine (one initialization-local boolean suffices),
copying primary content roots (unrelated asset mounting), and replacing native
localization (would lose selected/system language and SDL3/KPF behavior).

Reader contract: exact English path only; 64-bit PACK size, header/directory
subtraction bounds, existing directory validator, checked seek/read, native
allocation with NULL checks, 16 MiB text cap, trailing NUL, cleanup on every
failure. Do not export an unused length-out API. Keep the selected filename
across ordinary game/language reloads; reread text through native lifetime.

Expected production addition under 200 lines. Reopen the design if it requires
another retained service, altered mount rules or duplicated store policy.
Smallest end-to-end proof, after all implementation: a classic portable basedir
with no native English table prints text from a separate rerelease pack, while
active game/direct-root tables and native non-English tables retain precedence, with
no maps/progs/textures imported from that pack.

## Requested review and final qualification

Bounded local requested-Astra Max design advisory: verify the above source facts,
then challenge discovery order, file/allocator bounds and native behavior.
No renderer/network/FGD re-review, nested agents, writes or executable checks.
Effective settings are not exposed by the agent API, so do not describe the
result as a certified senior-review-skill pass. Main records disposition before
delegated implementation. Source review after integration remains advisory.

Final software cases: explicit/sibling/Steam precedence; invalid explicit root;
`-norerelease` and `-nosteam` with explicit root; exact English versus requested
non-English and existing English fallback; active game replacement and KPF
fallback; malformed/truncated/large PACK and optional allocation/read/seek
failure; repeated language reload/game switch/shutdown; unchanged model-only
mount and Steam API activation. No tests or builds run at this planning stage.

## Requested-Astra design disposition before coding

Local advisory requested `gpt-6-astra` / `max`; effective settings remain
unexposed. Main spot-checked the actual direct-root loader and reference's
whole-file lookup. This is not a certified senior-skill pass.

| Recommendation | Disposition |
| --- | --- |
| P2: the initially proposed hook displaced existing direct-root overrides | Adopt. Order is native game search, native direct-root files, separate exact-English PACK, then native KPF. Hook inside the existing `!rw` branch before KPF opens. |
| Distinguish invalid explicit operand from a trailing flag without an operand | Adopt. An explicit operand suppresses automatic fallback even if unusable; no operand follows the inherited automatic branch. |
| Preserve whole-file rather than missing-key fallback | Adopt clarification. A successfully loaded native table wins as a whole; do not merge a second rerelease dictionary for its missing keys. FGD supplementation remains its separate existing contract. |
| One filename and one local eligibility result; reuse native Steam resolution, validator and allocator | Adopt. No mount/service/discovery replacement or additional persistent policy. |

Production file/scope estimate remains unchanged. Software proof is deferred;
source review establishes the implementation contract only.

## Implementation integration checkpoint

The copied optional reader and discovery adapters are implemented in the one
planned file: 123 additions/3 deletions. Native 64-bit file sizes, directory
validation, checked seek/read and matching allocation/free replace the primary
API assumptions. The filename is not registered as a content root or search
path. Existing explicit model-only activation remains separate.

Main reviewed the complete diff and corrected an introduced argument-cursor
interaction before acceptance: the localization selector must use its own
`rerelease_parm`, preserving the existing `i` used by native `-basegame`
iteration. Add final software coverage for `-basegame` before and after
`-rerelease`; this patch must not reorder native game-data selection.

The loader hook is after both native direct-root attempts, before KPF; the
existing parser and reload/shutdown own its native allocation. Final source
advisory accepted the current corrected diff with no remaining P1/P2 finding.
It independently checked flags/discovery, native basegame cursor, Steam API and
unchanged model mount, PACK validation/cleanup and whole-file precedence/ownership.
This remains source-only advisory, with unexposed effective model/effort settings.
Scoped whitespace review passes; no
builds, compiler checks, engine probes, fixtures, tests or benchmarks ran.
