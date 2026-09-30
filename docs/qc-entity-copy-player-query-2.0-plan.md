# QC entity copy and player-query boundary repairs

Date: 2026-09-30. Production is confined to three existing wrappers in
`Quake/pr_ext.c` on `2.0`. Primary master `51b452c0` in read-only
`quakespasm-openvr/Quake/pr_cmds.c` is the behavioral reference.

## Verified current differences

| Wrapper | Actual current evidence | Reference adapter |
| --- | --- | --- |
| `PF_copyentity` | Copies from `&src->v` using `qcvm->edict_size - sizeof(entvars_t)`. Native `PR_LoadProgs` computes stride as QC field bytes plus native header and rounds it for pointer alignment. This subtraction is neither the field payload nor a header-free stride and depends on native/debug layout. A free source/destination only warns and still mutates; linking is unconditional in both VMs. | Copy primary's `progs->entityfields * 4` payload count, free-entity world return and SSQC-only no-touch relink. Keep destination native `alpha`, `sendinterval` and `sendinterval_default`; its scale is represented through QC fields rather than the primary header. Skip only the byte copy on self-copy, preserving metadata/link/return behavior. |
| `PF_edict_for_num` (`ftoe` / `edict_num`) | Calls native `EDICT_NUM` without the primary valid-slot guard. | Copy primary's integer conversion and `0 <= num < qcvm->num_edicts` guard, mapping invalid indices to world. Retain native entity encoding and access. |
| `PF_cl_playerkey_internal` | Checks only `MAX_SCOREBOARD` before dereferencing `cl.scores`. Native server-info parsing allocates exactly `cl.maxclients` score records, and `CL_ClearState` frees then zeroes the pointer. | Copy primary's null-scoreboard and `cl.maxclients` bounds in addition to the existing maximum. Keep sorted-negative selection, native values, temporary results and native extra userinfo lookup. |

Native and pinned QSS-M share the old `copyentity` length/link implementation;
copying that upstream wrapper again would preserve the demonstrated defect.
Primary already has the necessary payload and VM corrections. A valid payload
copy remains shallow, as in the existing/reference builtin; no new string,
entity, spatial or rendering ownership scheme is introduced.

`copyentity` continues to use native `ED_Alloc` for an omitted destination.
The free-entity guard remains after destination resolution, matching primary's
call order; this slice does not invent allocation rollback or redefine invalid
raw entity references. The primary's out-of-range entity-number policy is
copied at the existing numeric-to-entity wrapper instead.

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
self-copy, free entities, metadata, and observed SSQC no-touch linking versus
CSQC isolation. Check negative/equal-to-count/beyond-count entity indices and
last valid slot; scoreboard absent, small server allocation, last valid and
out-of-allocation slots, negative sorted selection, names/colors and additional
native userinfo. Keep normal server and desktop/VR cross-play behavior.

Malformed raw entity references, nonfinite/out-of-range float-to-int inputs,
and shallow zoned-string alias lifetime are not certified by this bounded
repair. Broader QC interface and end-to-end migration qualification remain
required; matching wrappers alone do not prove them.
