# QC entity copy and player-query boundary repairs

Date: 2026-09-30. Production is confined to three existing wrappers in
`Quake/pr_ext.c` on `2.0`. Primary master `51b452c0` in read-only
`quakespasm-openvr/Quake/pr_cmds.c` is the behavioral reference.

## Verified current differences

| Wrapper | Actual current evidence | Reference adapter |
| --- | --- | --- |
| `PF_copyentity` | Copies from `&src->v` using `qcvm->edict_size - sizeof(entvars_t)`. Native `PR_LoadProgs` computes stride as QC field bytes plus native header and rounds it for pointer alignment. This subtraction is neither the field payload nor a header-free stride and depends on native/debug layout. A free source/destination only warns and still mutates. | Copy primary's `progs->entityfields * 4` payload count and free-entity world return. Keep destination native `alpha`, `sendinterval`, `sendinterval_default` and current-VM no-touch linking; its scale is represented through QC fields rather than the primary header. Skip only the byte copy on self-copy, preserving metadata/link/return behavior. |
| `PF_edict_for_num` (`ftoe` / `edict_num`) | Calls native `EDICT_NUM` without the primary valid-slot guard. | Copy primary's integer conversion and `0 <= num < qcvm->num_edicts` guard, mapping invalid indices to world. Retain native entity encoding and access. |
| `PF_cl_playerkey_internal` | Checks only `MAX_SCOREBOARD` before dereferencing `cl.scores`. Native server-info parsing allocates exactly `cl.maxclients` score records, and `CL_ClearState` frees then zeroes the pointer. | Copy primary's null-scoreboard and `cl.maxclients` bounds in addition to the existing maximum. Keep sorted-negative selection, native values, temporary results and native extra userinfo lookup. |

Native and pinned QSS-M share the old `copyentity` length/link implementation;
copying that upstream wrapper again would preserve the demonstrated defect.
Primary already has the necessary payload and VM corrections. A valid payload
copy remains shallow, as in the existing/reference builtin; no new string,
entity, spatial or rendering ownership scheme is introduced.

### Current-VM linking disposition

The native world owner is shared by SSQC and full-game CSQC. Actual client
initialization in `Quake/host.c` assigns `qcvm->worldmodel = cl.worldmodel` and
calls `SV_ClearWorld` before `CSQC_Init`. `Quake/world.c:SV_ClearWorld` initializes
the current VM's own area nodes; `SV_LinkEdict` links against that same VM's
world/area nodes. Native `PF_setorigin` uses this owner in both VMs. Therefore
primary's SSQC-only linking restriction is not copied: it would remove native
client-QC spatial updates without evidence of a required incompatibility.
The initial plan's linking-change premise is withdrawn before integration;
retain unconditional native no-touch linking for valid copies. No server/client
world replacement or new isolation layer is necessary.

`copyentity` continues to use native `ED_Alloc` for an omitted destination.
Reject an already-free source before destination resolution, then retain the
post-resolution free guard as well. This adapts primary's failure result without
copying its allocation-order defect; it does not invent allocation rollback or
redefine invalid raw entity references. The primary's out-of-range entity-number
policy is copied at the existing numeric-to-entity wrapper instead.

### Allocation-order correction after source review

Local Astra source review of `d5cf72e9` confirmed the payload, metadata, query
bounds and retained current-VM linking. It also identified the preexisting
omitted-destination ordering hazard. Main verified `ED_Alloc`: the native FIFO
may return the same freed source slot, clear its QC payload and mark it live
before the old free guard runs; otherwise the rejected call can consume another
slot or hit the edict limit. Withdraw the initial plan's accepted ordering.
Add the same warning/world-return guard before `ED_Alloc` can run, retaining
the post-resolution guard for destination and allocation-hook state. Valid
copies keep the same native allocation and return behavior. Expected additional
scope: one six-line preflight in the existing wrapper, with no new owner.

## Reuse, scope and implementation

Keep the VM layout/loader, native allocation/free list, entity encoding,
metadata, server linking, scoreboard allocation and native userinfo owner.
Replacing these owners or duplicating a client/server entity service would
add state without addressing another demonstrated incompatibility. Expected
scope: approximately 15 changed lines in `Quake/pr_ext.c`; no registry,
numeric slot, capability, protocol, model, renderer or asset change.

Commit this plan before code. One authorized web GPT coding agent may edit
only the three named wrappers. It must read reference and native layout first,
not replace the whole file, and must not revert concurrent edits. Main reviews
the diff, integrates and requests bounded local Astra read-only correctness
review. No expensive new architecture decision is introduced by these copies.

## Final acceptance and limits

No builds/tests/compiler/runtime probes/fixtures until the full implementation
finishes; `git diff --check` is allowed. End-of-implementation Linux/ARM checks
must invoke both VMs with explicit/omitted destinations, full trailing QC fields,
self-copy, free entities with explicit/omitted destinations (including a reusable
source at the free-list head and an edict-limit case), metadata, and observed no-touch linking in each VM's
own world without cross-VM mutation. Check negative/equal-to-count/beyond-count entity indices and
last valid slot; scoreboard absent, small server allocation, last valid and
out-of-allocation slots, negative sorted selection, names/colors and additional
native userinfo. Keep normal server and desktop/VR cross-play behavior.

Malformed raw entity references, nonfinite/out-of-range float-to-int inputs,
and shallow zoned-string alias lifetime are not certified by this bounded
repair. Broader QC interface and end-to-end migration qualification remain
required; matching wrappers alone do not prove them.

## Source implementation checkpoint

Commits `d5cf72e9` and `7d7104d5` implement the payload, self-copy, free-operand
and allocation-order guards, numeric entity bounds and allocated scoreboard
bounds. Main retained native current-VM linking and native userinfo fallback.
A local requested-Astra Max follow-up found no actionable introduced defect in
this bounded source slice, including the reusable free-source allocation case.
Effective reviewer model/effort metadata was unavailable, so this is advisory
source acceptance rather than a certified senior-review gate. Only source
inspection and `git diff --check` were performed; final qualification is pending.
