# Keep serialized entity liveness authoritative during save restoration

2026-09-30. Before-code MOD-012 brief, baseline2b4f3ebf. No production edits
for this slice yet. The existing native parser, numbered load loop and FIFO
remain the owners; no new save format, entity registry or physics policy.

## Verified behavioral reference and incompatibilities

Retained primary51b452c018273647dcf94f4628a370267ff8fa91 ED_ParseEpair records
entity references without allocating their targets. Its ED_ClearEdict removes
free-list membership before admitting an overwritten numbered entity. Native
vkQuake4bc898f29073e8aa41069f0e79e3cb5a9eb73afa already has bounded forward
reference growth, FIFO allocation and post-load reconstruction. Reuse those.

Requested-Astra source audit found two donor-inherited P2 defects; main checked
the current ev_entity branch and actual Host_Loadgame_f loop:

- ED_ParseEdict frees a serialized empty block, but a later entity-valued field
  unconditionally removes the target from the FIFO and marks it allocated.
  Empty slot10 followed by slot20 with enemy10 revives10 even though its saved
  block is empty. Existing serializers allow such stale references. Final
  ED_RebuildFreeList cannot repair liveness already changed by this assignment.
- The numbered load loop clears an existing slot's free flag before parsing,
  without removing its existing FIFO entry. An empty parsed block then adds
  the slot again. Sparse saves/fastloads can overflow the transient FIFO before
  the final rebuild. The existing ED_RemoveFromFreeList helper is sufficient.

Spawn-before-think is already source-present: registered cvar, native client
gate, source-authorized spawn command and ClientConnect/PutClientInServer.
knowntoqc is admission, not a callback-completion latch, matching the references.
Do not replace that gate or expand into prediction/QC scheduling.

## Adapter versus ambient state or loader rewrite

Preferred: explicit save-restoration parser entry points, implemented as small
wrappers over the existing parser bodies with an internal reference-only
boolean. Keep existing public ED_ParseEpair, ED_ParseGlobals and ED_ParseEdict
contracts for console, initial map load and QC field/cvar consumers. Add only
saved-global/saved-edict entry points used by the existing numbered load loop.
Propagate the flag through the internal global/edict parser to the same epair
body. Save entity assignment records the reference without reviving its target.

Retain the native upper bound and growth/initialization for forward references.
For newly exposed slots in the saved-reference path, include the referenced
target in native zero/header initialization and ED_Free, so unparsed slots are
not stale live entities from an earlier fastload. Existing already-parsed free
slots keep their liveness. Serialized nonempty blocks remain admitted by the
load loop. Reject negative entity indices before pointer construction as well
as the existing upper bound; this is a narrow index-domain correction, not a
new numeric-text parser. Native ordinary map/QC/console behavior otherwise
retains its original allocation policy.

Before clearing an already-free overwritten slot in Host_Loadgame_f, call
existing ED_RemoveFromFreeList. Keep the final ED_RebuildFreeList(true), reserved
world/client filtering, saved-player policy, native linking and all save dialects.

An ambient sv.loadgame gate is unsuitable: it remains true through reconnect,
and some fastload paths do not set it. A persistent parser-context flag creates
an unnecessary Host_Error/longjmp reset obligation. A two-pass save parser or
replacement FIFO introduces more state/policy than these two demonstrated
boundaries need. Explicit internal parameters avoid those lifetime obligations.

Expected production write set: Quake/pr_edict.c, Quake/progs.h,
Quake/host_cmd.c only; roughly40–80 added wrapper/guard/parameter lines. Reopen
if another ownership/state-machine/format layer or broader lifecycle reset is
required. Do not add fresh prediction/contact cleanup without source evidence.

## Before-code review and final qualification

Request bounded local Astra design advice to challenge wrapper/flag necessity,
forward-reference initialization, both saved globals and edicts, reserved slots,
ordinary parser compatibility and Host_Error behavior. Main verifies the
load-bearing claims and records disposition before delegated implementation.
Final exact-patch source review follows. Effective model settings remain
uncertified; source findings are not runtime signoff.

After all implementation, use actual native v5/v6/v7 and fastload save paths
with stale backward references to empty blocks, forward global/edict references,
nonempty targets, sparse high indices, reserved clients and serial slot reuse.
Validate liveness and unique FIFO membership through actual allocation after
restore, not just direct helper calls. Negative/max-bound failures and ordinary
map/QC field assignment retain meaningful coverage. No save-format extensions,
VR demos, deployment or user live tests. Linux/ARM qualification comes last;
no builds/tests/compiler/probes/fixtures/benchmarks before implementation ends.

## Before-code Astra disposition

Requested local Astra xhigh accepted the explicit saved-parser adapter with one
P2 debug-conversion correction and a native header-initialization clarification.
Main checked release/debug byte-offset encoding and NUM_FOR_EDICT range rules.
No implementation or software verification follows merely from this advice.

| Recommendation | Disposition |
| --- | --- |
| Reuse parser bodies with explicit saved-global/saved-edict entry points. | Adopt. Private parameterized cores and existing public wrappers preserve ordinary console/map/QC contracts. Declare private epair core before the earlier globals body; no exported saved-epair API. |
| Use ordinary EDICT_TO_PROG for saved empty targets. | Correct P2. Debug conversion rejects free edicts. After checked growth/initialization, encode the same native byte offset directly for saved references; never revive targets just to satisfy a debug assertion. Ordinary conversion/assertion remains. |
| Initialize only newly exposed slots, including referenced target. | Adopt, with baseline=nullentitystate and debug owner/pointer/index restored before ED_Free. Native count growth precedes free because NUM_FOR_EDICT checks it. Existing target liveness remains untouched. |
| Detach an already-free overwritten slot before numbered admission. | Adopt existing ED_RemoveFromFreeList; retain final reconstruction, reserved/client filtering, excess-slot retirement and all saved-player branches. |
| Drop growth, use ambient state or reparse saves twice. | Reject unsupported equivalence/replacement. Per-block linking can encounter forward references; retaining native bounded growth changes less. Explicit parameters have no persistent error-unwind state. |

The three-file40–80-added-line bound remains. Reopen if another state owner,
registry/save format or broader physics cleanup is needed. Final actual-patch
source review and end-of-implementation Linux/ARM qualification are pending.
