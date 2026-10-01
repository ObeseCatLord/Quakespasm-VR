# Final Linux/ARM qualification ledger

2026-10-01. In progress; goal remains incomplete. Source entry snapshot
`8e48f41c9932a5475c1228b9143e80c01bccbb07`, immutable git archive. Final checklist
and all185 feature dispositions remain authoritative; narrow results below do
not close the full scope. No deployment or changes to main/master/assets.

## Local full compilation: first finding and bounded correction

Meson debugoptimized, required SDL3/WAVE/MP3/FLAC/Vorbis/Opus and CURL, host
GCC16.2.1/Vulkan1.4.357. Steam Audio disabled for this preliminary compile;
full Steam Audio4.8.1 portable native builds are separately required.

After the first correction, compilation found the copied entity-enumeration
fingerprint cache uses the donor VM field qcvm->crc, absent in vkQuake. Native
progs.h owns unsigned-short progscrc, initialized from the entire loaded file
in PR_LoadProgs before byte swapping. Smallest correction: use that existing
native field at both cache comparison/assignment; retain pointer/CRC cache,
fingerprint, SSQC guard and traversal. Two expression substitutions, no new
VM identity owner. Actual loaded-QC reload behavior still requires qualification.

The next compiler pass reached co-op respawn: an immutable death-origin snapshot
was passed to the native mutable-vector trace API, and GCC warned that a local
anchor-angle vector may be uninitialized across the relocation branch. Preserve
both APIs/policies: copy the snapshot into a local vec3 at the existing caller,
and initialize the local angle vector (the validated anchor branch still writes
all three components before use). No cast, mutable saved snapshot, trace rewrite
or warning suppression. Rebuild and retain actual co-op qualification as pending.

View-offset cleanup used VectorClear, defined only in pmove.h and unavailable
to the native view module. Use existing mathlib VectorCopy(vec3_origin, offset)
at that one cleanup line; avoid adding a movement dependency or confusing the
unrelated dynamic-vector Vec_Clear API. Smoothing lifecycle acceptance remains
pending after the full rebuild.

Gesture attack merging needs the shared BUTTON_ATTACK command-bit definition
owned by pmove.h. Include that existing header in vr_input.c, rather than
duplicating a protocol bit or weakening the gesture/trigger policy. Header-only
compile correction; existing gesture fixtures and native input qualification
remain required.

World-wheel occlusion likewise passes immutable frame endpoints to native
CL_TraceWorldLine's mutable-vector interface. Copy both endpoints into local
vec3 inputs at that existing call; retain actual trace/impact/distance checks
and avoid changing the shared native particle API or casting away const.

First build stopped at gl_model.c:4828/4829 under native warnings-as-errors:
the inherited Copper axe cache passes const input to ReadLongUnaligned(byte*).
The helper only memcpy-reads into a local int. Smallest correction: accept
const byte* at that existing helper, retaining all callers and byte-order logic.
One-line signature change, no casts, replacement parser or warning suppression.
Rebuild the full configuration after the correction; portable results cannot
qualify a newer tree until that tree is built too.

## Active native portable builds

Second native portable attempt at25fbfd47 progressed through dependencies. ARM
built native Steam Audio4.8.1 but Meson setup rejected --debug=true (and --strip
is also a switch). Use Meson's native -Ddebug=true/-Dstrip=false options, retaining
release optimization and symbols; no feature fallback.

On amd64, Valve's Linux AVX branch injects legacy -fabi-version=6, which breaks
GCC13 libstdc++ future/unique_ptr construction. A minimal <future> source in the
same pinned container fails with that flag and passes with the native ABI.
Retain AVX/performance: remove only that legacy compile option in the selected
Linux SDK source before CMake. All SDK C++ objects are built together, private
implementation stays hidden, engine consumes the stable phonon C API. Preserve
original source archives and record the exact in-recipe adjustment in options
receipts/source-access artifacts. Do not weaken standard-library errors or
disable spatial audio. Final native SDK/client linking remains required.

Local amd64 and Foundry native arm64 container routes launched from the same
immutable entry archive and committed production packaging recipes. Both use
pinned Ubuntu24.04/dependency recipes; builds, staging, relocation and negative
artifact checks are pending. Foundry source/output is isolated under /tmp,
without replacing its server or assets.

## Native metadata qualification

Actual desktop and simulated-XR full-engine runs timed out before signon.
GDB proved thousands of host frames, active server/connected client, signon0,
the generated pext offer sent and drained, and server pextknown=false. Native
SV_Init used console-only Cmd_AddCommand for pext while the migrated command
dispatcher rejects src_client for that registration. Pinned QSS-M registers
pext with Cmd_AddCommand_ClientCommand. Host name/color/spawn/begin/prespawn/
enablecsqc registrations already use the proper client boundary.

Bounded repair: copy QSS-M's one-line pext registration into current SV_Init,
retaining native Cmd_ExecuteString source policy, handler and separate console
diagnostics. Do not broaden the dispatcher whitelist, weaken permissions or
call the handler directly to bypass real transport. Rebuild and rerun actual
desktop/stereo signon; attach this required fix to existing negotiation/C02
acceptance, not a new feature. Old prepared fixtures did not establish this path.

Both portable builders reached pinned SDL3 configuration and failed because
the direct Ubuntu dependency selection omitted XTEST's libxtst-dev. SDL's own
configuration requires that dependency and official Linux build documentation
lists it: https://wiki.libsdl.org/SDL3/README-linux#build-dependencies.
Add that package to the existing shared sources.json list; preserve SDL features,
native builds and exact binary/source/notice receipt capture. One list addition,
no backend disable or packaging bypass. Rebuild both images and retry a new
immutable source archive containing all source repairs; old failed snapshots
remain diagnostic evidence only.

One verified gpt-6-luna/xhigh worker owns the new native publication fixture,
runner and bounded receipt only. It must exercise actual sender/parser owners
and report phase/resource boundaries; older prepared-signon fixtures are not
accepted as complete C02 lifecycle evidence. Result pending.

## Completed bounded results and remaining failures

Full preliminary host compilation/rebuild succeeded through `a1df3ffd`, including
all generated shaders and native warnings-as-errors. Steam Audio remains disabled
only in this preliminary host configuration; full enabled builds are required.

The actual full-engine simulated-Monado/Vulkan run passed24 probes with zero
validation errors/hazards and exit0. Native tasks/GPU lightmaps, stereo SSAO,
OIT/MSAA/indirect variants, camera changes and firing were exercised. Main inspected
initial rendered two-eye world/HUD output. Exact command/evidence/limits are in
[the GPU receipt](final-openxr-gpu-qualification-2.0.md). No actual gaze/foveation
or headset claim follows.

Eighteen of19 standalone fixtures pass after six narrow test-only current-API
adaptations, reviewed and integrated in `aacc54e9`. The broad controller input
fixture still fails to link because its stubs are stale/incomplete. This is an
unfinished verification case, not an established production feature omission.
[Receipt and commands](standalone-vr-final-qualification-2.0.md) distinguish
helper/spies, simulated dispatch, real headless creation and loaded rendering.

Desktop rendered signon4 but reports Vulkan WRITE_AFTER_WRITE hazards; normal
process quit aborts134 with allocator corruption. Independently built untouched
vkQuake `4bc898f2` reproduces ten matching hazards and allocator corruption/
exit134. Root-cause identity remains unproved. Both software acceptance defects
remain open. The [focused Astra disposition](final-renderer-qualification-2.0-review.md)
requires exact attachment/pass mapping and teardown localization before narrow
fixes, preserving native renderer/lifetime owners.

Third complete portable build attempt: same immutable `48e026e0` archive on
local amd64 and isolated native Foundry arm64, including all prior source/recipe
repairs. The amd64 SDK/dependency phase completed, then GCC13 -O3 warnings-as-errors
stopped engine compilation: gl_rmain.c:939/943/955 effective_view may be
uninitialized after R_StereoSceneView, and gl_vidsdl.c:1600/1631 cached format
result may be uninitialized. Native ARM also ended with the same effective_view
failure; no cached-result warning is recorded there. These are confirmed
portable build failures, not established runtime failures; source contracts
must be checked before narrow initialization/guard repairs. No warnings disable,
feature fallback or native owner rewrite is justified. Final artifact acceptance
remains pending.

A substantive upstream merge rehearsal was executed in a disposable shared clone
at `a1df3ffd`, merging pinned official `0d812138` (36 upstream commits since
baseline). Fourteen files/35 blocks conflict. Main inspected the actual blocks
and documented native changes, adapter owners and coupled risks in the
[rehearsal receipt](upstream-merge-rehearsal-final-2.0.md). Enumeration/documentation
is complete; conflict resolution and merged-build/behavior evidence are not,
so maintainability acceptance remains open. No production branch or history
was merged/rewritten.

All results are partial acceptance within their named boundaries. The full
eight-group final checklist remains open; feature source presence alone does
not establish behavior. The [final exhaustive senior reconciliation](final-checklist-qualification-2.0-review.md)
records source closure, these defects and all remaining groups without narrowing
scope. No additional missing source feature was established; software/delivery
acceptance and final integration review remain open.
