# Inherited QuakeC builtin compatibility

## Evidence and ownership

The local Astra registry audit found inherited MOD-001/004/005/006 ABI gaps
between primary `quakespasm-openvr/Quake/pr_cmds.c` and the existing vkQuake
registry in `Quake/pr_ext.c`. This is a source audit, not runtime acceptance.
The primary checkout is read-only; changes belong only on `2.0`.

Keep vkQuake's VM, builtin registry, lazy binding and resource owners. Adapt
demonstrated inherited contracts at those boundaries; do not import a second
VM or replace the donor registry. Occupied numeric slots must retain donor
semantics for donor programs. Any inherited conflicting binding needs verified
function-name, VM and declared-slot evidence before changing dispatch.

## First slice: successful extension discovery

Primary `PF_builtin_find` initializes the result to zero, compares names without
case sensitivity, and returns immediately after finding the registered number.
Destination `PF_builtinsupported` assigns a match but then unconditionally
overwrites the result with zero. Thus `builtin_find("sin")` returns zero even
though the existing extension registry maps it to 60 in both VMs.

Copy the primary lookup control flow into the existing destination helper:
initialize zero, reject an empty name, compare without case sensitivity, return
on the first match. Reuse the destination's assigned numbers and existing
extension table. This slice introduces no new entrypoints, permissions,
capability advertisements, number allocation or invocation policy. It does not
claim that core builtin discovery or the rest of the inherited ABI is complete.

## Registry audit findings (initial checklist)

These require handler and VM-contract verification before implementation:

- Core-name lookup is missing from `builtin_find`.
- Inherited numeric conflicts: SSQC `localsound` 80 versus donor `infokey`;
  EX-flags/path 90/91 versus `tracebox`/`randomvec`; EX-flags 430 versus
  `te_lightning3`; CSQC `dprint` 277 versus `frameduration`.
- Missing inherited registrations: `cvar_setf` 176; search 444-447; buffer file
  operations 535-536; SSQC `setcolors` 401; CSQC `drawline` 315, `cprint` 338,
  `sendevent` 359 and `getresolution` 608. Reuse existing cvar, filesystem,
  string-buffer, userinfo and draw services. Verify event transport separately.
- CSQC rain/snow 409/410 permissions differ from primary. Do not advertise
  support where the current VM has no handler.
- Numeric/name aliases differ: `strconv` 249/224; `draw_getimagesize`,
  `drawcolorcodedstring`, `drawcolorcodedstring2`; `ex_finalefinished` casing.
- Capability aliases and primary case/disable behavior differ. Existing
  handlers alone do not prove equivalent semantics or justify advertisement.
- Cursor/font 343/357 lose only inherited no-op fallbacks. Finale/EX-flags/path
  stubs are not new functional implementations; fog backend support was outside
  the registry audit. Do not promote these to functional claims.

## Acceptance

Use a bounded local Astra source review of the first helper change. Keep all
builds, compiler probes and runtime tests until implementation is finished, as
the user requested. At final software verification, exercise `builtin_find` in
SSQC and CSQC for registered names, mixed-case names, absent/empty names and a
named dynamic-number entry; verify that invoking returned numbers still follows
the existing VM permission/binding rules. Add core and collision cases when
their adapter slices are implemented. No assets or deployed game state change.

## First-slice source disposition

The helper change is implemented. Personal local Astra Max accepted it with no
introduced P1/P2: unsuccessful lookup retains zero, successful lookup returns
the assigned number immediately, and comparison follows primary's case behavior.
The reviewer verified existing documented/dynamic number initialization and
confirmed that allocation and VM invocation permissions are unchanged. No
builds, tests or probes were performed. The remaining audit findings above are
still open; this acceptance covers only the first slice.

## Second slice: cvar and HUD service adapters

Primary `Quake/pr_cmds.c` implements `PF_cvar_setf` by formatting the float with
`%g` into a 32-byte local buffer and calling `Cvar_Set`. Copy that wrapper into
destination `Quake/pr_ext.c` and register 176 for both VMs. Neither the donor
registry nor the core builtin tables occupy 176. Reusing `Cvar_Set` retains
read-only/locked checks, cvar callbacks and existing unknown-variable behavior.
Do not replace it with a second cvar service or change CSQC cvar read overlays.

Copy primary's `PF_cl_cprint` wrapper (`SCR_CenterPrint(PF_VarString(0))`) and
register 338 for CSQC only. Reuse the current centerprint string, wrapping,
timing and desktop/VR presentation. No second message queue or HUD owner.

Add CSQC `getresolution` 608 using the existing `SCR_GetCSQCDisplay` helper.
Primary returns console width/height, but destination `Sbar_DrawCSQCHud` passes
the virtual HUD extent (`display.width / display.scale`, height likewise) to
`CSQC_DrawHud`. The existing helper also owns relative desktop scaling and the
VR panel override. Return that same extent with zero Z so the new query agrees
with the draw callback and clipping/cvar adapters. Copy the primary result
shape, adapting only the demonstrated coordinate boundary. No server handler.

Add the primary's `draw_getimagesize` 318 and `drawcolorcodedstring` /
`drawcolorcodedstring2` 326 exact-name aliases to existing CSQC handlers. The
documented numeric aliases share the same handlers and allocate no dynamic
numbers. Preserve primary's best-effort QSS/FTE drawstring argument contract;
do not claim a new DarkPlaces signature implementation. Add the lowercase
`ex_finalefinished` exact-name alias to the existing SSQC fallback, retaining
the original donor spelling. This is an inherited fallback, not new finale
detection. Its ordinary dynamic number is allocated by the existing registry.

Primary `PF_cl_drawstring` uses `mu.colour` and its alpha for each glyph after
`PR_Markup_Parse`. Destination passes the original RGB/alpha to
`DrawQC_CharacterQuad`, losing its parser's color and half-alpha changes. Pass
the parser's existing per-glyph RGBA to that same helper. Plain strings and raw
strings keep their existing behavior. No shaders, pipelines, render targets or
new geometry path are needed.

The parsers share the per-glyph RGBA contract, not identical parsing policy:
primary disables caret markup with `false`, while destination retains the
donor's `pr_checkextension.value` gate. Preserve that donor parsing policy.

All changes stay in `Quake/pr_ext.c`; occupied-slot compatibility, core-name
lookup, file/search/buffer lifetimes, events and capability advertisement remain
separate slices. Estimated production scope: three small wrappers, seven registry
entries and one existing draw call argument correction. Reopen this slice if it
needs VM dispatch changes or another state owner.

Acceptance: local Astra source review of wrappers, VM registration, alias
number allocation and coordinate/color boundaries. At end-of-implementation
software verification, cover numeric and named lookup/invocation in permitted
VMs; forbidden SSQC HUD calls; ordinary, absent and protected cvars; concatenated
centerprint timing; desktop absolute/relative scaling and VR panel extents;
plain strings, color/reset/half-alpha markup and unchanged raw strings. Builds,
tests and probes remain deferred. Hardware rendering qualification stays with
the user.

## Second-slice source disposition

Personal local Astra Max accepted the actual cvar/HUD patch with no introduced
P1/P2. The reviewer verified primary wrappers/aliases, existing number allocation
and VM-specific invocation, shared destination HUD extents and per-glyph RGBA
consumption. The documentation parsing-policy correction above is accepted;
no new parser policy is introduced. Source acceptance does not prove identical
primary markup output or runtime/desktop/VR rendering. No builds, tests, compiler
runs or probes were performed. Other registry gaps remain open.

## Buffer file slice: native resource adapters

Register inherited `buf_loadfile` 535 and `buf_writefile` 536 for both VMs.
Copy primary's append-line algorithm, CRLF handling, sparse-string writes and
optional start/count arguments onto the existing destination `strbuflist`,
`PF_bufstr_add_internal`, `qcfiles`, and their existing base offsets. Require
each supplied buffer/file to belong to the current VM, as primary's getters do.
No new handle table, lifetime or shutdown owner. Reject nonfinite/out-of-range
handles before converting them to indices; retain ordinary fractional truncation.

Destination `COM_LoadFile` returns NUL-terminated heap storage (`Mem_AllocNonZero`)
instead of primary's temporary-hunk `COM_LoadTempFile`. Free that storage after
appending lines. An absent file returns zero and leaves the buffer untouched;
an empty readable file returns one without appending a line. Preserve primary's
line behavior, including no synthetic final blank line after a trailing newline.

Adapt read paths through the existing `QC_FixFileName` policy and normalized
`data/` path with its permitted read fallback, matching destination `fopen`.
Do not import a second path policy from primary. This intentionally retains
donor data-path precedence and config read restrictions; it is not a claim of
byte-identical primary filesystem precedence. Read packaged files through the
existing engine filesystem, not direct OS paths.

Writing uses an already-open, current-VM, non-read-only native QC file handle;
skip sparse NULL entries and append newlines exactly as primary. Clamp optional
start/count against the existing buffer extent before integer conversion/addition
to avoid overflowing the range calculation. Nonfinite ranges return zero.
Report an immediate write error as zero rather than primary's unconditional
success; no transactional or implicit flush guarantee is added. Existing file
close/shutdown and buffer shutdown remain authoritative.

Expected scope: two wrappers in `Quake/pr_ext.c` and two nonconflicting registry
entries. Source-review actual ownership, temporary storage release, path
precedence, parsing and range math. Final software verification must cover both
VMs, cross-VM/closed/reused handles, missing/empty/CRLF/blank/unterminated text,
existing content append, pack reads/data precedence/rejected paths, read-only
output, sparse buffers, negative/fractional/huge/nonfinite ranges, write failures
and reload cleanup. Builds/tests/probes remain deferred until all implementation
is finished; no deployed assets or configs change.

## Buffer-file source disposition

Both entrypoints are implemented on native owners. Personal local Astra Max
verified parsing, heap-text release, current-VM ownership, donor path policy,
read/write modes, sparse output, range math and both VM registrations. It found
one P2: at an extreme file-table size, float subtraction could round a validated
handle into an out-of-range index. The wrapper now checks the converted file
index before any table dereference. Astra accepted that correction with no
remaining P1/P2 in this slice. No builds/tests/compiler runs or probes were
performed. Existing unrelated native QC API issues were outside the review;
full filesystem and VM lifecycle acceptance remains at final software checks.

## Alternate string-conversion slot

The initial alias checklist also requires `strconv` 249, alongside existing
224. Main compared actual primary/destination conversion wrappers and numeric,
punctuation and alphabetic helper bodies: the contracts match, including
variadic concatenation, temporary-string limits and color/case conversion.
Neither destination core tables nor its extension registry occupy 249. Add the
primary's alternate registration for both VMs, reusing `PF_strconv` directly.
Keep 224 first so named discovery and `#0` binding keep their existing result;
explicit 249 calls use the same native handler. No new converter, dynamic number
or capability claim. Source comparison covers this routine registration; final
software checks must compare numeric invocation at both slots and named binding.

## Current source checkpoint

The initial checklist above records audit findings, not today's unfinished list.
The following slices now have bounded local Astra source acceptance:

- Successful extension-name discovery; cvar/HUD wrappers and name aliases;
  inherited conflicting numeric bindings, using the existing loader and registry.
- Buffer-file wrappers and [file search](qc-file-search-2.0-plan.md), using native
  VM-owned resources and platform enumeration; alternate `strconv` 249.
- [Capability queries](qc-capability-query-2.0-plan.md): primary case/global
  disable behavior, native protocol/per-capability gates and three verified
  aliases. Other missing advertisements require contract evidence, not merely
  an existing handler.
- [CSQC rain/snow](qc-weather-2.0-plan.md), calling the existing native
  received-weather backend.

The [line-drawing adapter](qc-drawline-2.0-plan.md) also has local Astra source
acceptance. Remaining demonstrated inherited-call work includes core builtin-name
discovery and SSQC setcolors. The paired
[client/server event adapter](qc-events-2.0-plan.md) has local Astra design and
source acceptance; the inherited cursor/font fallbacks also have bounded local
Astra source acceptance. Remaining VM
permission/resource lifetimes and broader contracts still require their bounded
comparison and final software acceptance.

Event comparison established that destination initially lacked both
`clcfte_qcrequest` 81 and its server reader, not just `sendevent` 359. The implemented
paired adapter copies the primary/QSS-M typed request and `CSEv_*` dispatch onto
native message and VM owners, with reviewed framing/lifecycle corrections.
A registry-only wrapper would not deliver the inherited behavior. No replacement
transport or generic RPC layer was introduced for this standard call.

Fresh color comparison also identifies an existing adapter: QSS-M's
`PF_setcolors` calls native `SV_UpdateInfo` for top/bottom colors. Copying primary's
legacy broadcast alone would leave modern userinfo clients inconsistent. Both
QSS-M and destination `SV_DecodeUserInfo` clamp palette nibbles 14/15 to 13 and
decode names when any info key changes; primary's direct call preserves the raw
byte and does not touch names. The color slice must explicitly resolve these
boundary differences before claiming primary parity. Reuse the existing userinfo
and broadcast owners; do not create a separate replicated color service.

All source checkpoints remain distinct from runtime parity. No builds, tests or
compiler probes were run during these implementation slices; deferred Linux/ARM
qualification must cover the complete interfaces and relevant negative paths.

## Inherited cursor/font fallback slice

Primary `Quake/pr_cmds.c` has CSQC-only `setcursormode` 343 as an empty function
and `loadfont` 357 returning zero. These are ABI fallbacks, not implemented cursor
grabbing/hardware-cursor or external-font features. Destination has neither entry;
both slots are absent from its extension registry and beyond the core tables.

Copy these two exact small wrappers into destination `Quake/pr_ext.c` and register
their primary numbers for CSQC only. Retain the existing input/cursor/font owners
and bitmap text renderer. Mark both registry descriptions as `stub.` using the
native convention, so existing lazy stub diagnostics/support checks retain their
policy. Do not add font/cursor capability advertisements or pretend that these
fallbacks load any resource. No VM dispatch, platform input or rendering change.

Scope: two wrappers and two entries in `Quake/pr_ext.c`, with source comparison
and bounded local Astra review. Final deferred software checks must cover explicit
and named invocation, numeric noncollision, CSQC no-effect cursor calls, zero
font return and SSQC rejection; ordinary bitmap text must still use the native
renderer. No builds/tests/probes before implementation is finished.

Fallback source acceptance: local Astra verified the exact primary empty cursor
wrapper, zero font return, unique registry slots and null SSQC handlers. Main
source inspection also confirmed the native core tables end below slots 343/357.
No cursor/font resources, capability advertisements or renderer changes were
added. This accepts the source adapter only; deferred invocation checks remain.
