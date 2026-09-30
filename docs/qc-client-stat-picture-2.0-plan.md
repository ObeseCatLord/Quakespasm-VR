# QC client stat and picture wrapper repairs

Date: 2026-09-30. Implementation stays on `2.0`; primary is read-only.
Behavioral reference is primary master `51b452c0`, `Quake/pr_cmds.c`
`PF_cl_getstat_int`, `PF_cl_getstat_float`, `PF_cl_getstat_string` and
`PF_cl_precachepic`. These wrappers are CSQC-only in both engines.

## Verified gaps and minimal adapters

| Call/trigger | Verified current difference | Planned repair |
| --- | --- | --- |
| `getstati`, `getstatf`, `getstats` with index equal to the array count | Native wrappers test `>` instead of primary's `>=`, then access past the end. | Copy primary's exclusive upper-bound condition in all three wrappers. Retain successful numeric/bitfield/string paths and temporary-string ownership. |
| One-argument `precache_pic` | Native wrapper reads parameter 1 regardless of `qcvm->argc`; primary uses zero flags when omitted. | Copy the optional-argument guard at the wrapper. |
| Missing picture with default flags versus `PICFLAG_BLOCK` (bit 9) | Primary returns the original name for the ordinary request and zero for a failed blocking request. Native returns zero for both and lacks the public flag constant. | Add the existing reference flag bit to `draw.h`; copy the primary return predicate around native `DrawQC_CachePic`. |

The native picture cache is synchronous: `DrawQC_CachePic` either finds a
cached image or calls the existing WAD/image load path. The blocking flag
therefore adds the reference failure-reporting contract, not an asynchronous
loader or wait state. `cachepic_t.picflags` is stored by native loaders; no
other consumer of that field was found in the checked drawing/texture owners.
Existing WAD fallback, leading-slash normalization, hash lookup, texture
flags, native image precedence and rendering remain reusable and unchanged.

Replacing the picture cache with primary's separate QC picture table would
duplicate native storage and lifetime policy without fixing another proven
incompatibility. Reject that replacement. No renderer, canvas, shader,
Vulkan-resource, VM, stat storage, capability or registry rewrite is needed.
Expected production scope is `Quake/pr_ext.c` and one enum member in
`Quake/draw.h`, approximately six changed conditions/declarations.

## Implementation and acceptance

Commit this plan before production. Main copies/adapts the reference guards
and predicate; request one bounded local Astra read-only correctness review
after the edit. This is a routine existing-boundary repair, not a new expensive
architecture decision. Review must check real reference/native paths, unchanged
SSQC refusal and registry slots 317/330/331/332, mutex release, picture cache
ownership and the exclusive stat bound. No nested delegation or broader rewrite.

Builds, tests, compiler/runtime probes and fixture additions remain deferred
until the full implementation is finished. `git diff --check` is allowed.
Final Linux/ARM VM qualification must cover last valid, equal-to-count,
negative and beyond-count indices for all three stat kinds, ordinary numeric
and string results, omitted flags after a preceding flagged call, cached and
missing images with default/block flags, native WAD/external precedence and
SSQC rejection. The existing `getstatf` bit-extraction path and malformed
numeric argument conversion are unchanged by this slice and are not certified
by it; whole-registry/interface acceptance remains required.

## Implementation and source disposition

`b2024418` implements the planned repair through one web GPT coding agent;
main inspected the actual diff against primary before committing it. A local
requested-Astra/Max read-only review found no actionable introduced defect.
Main spotchecked the three arrays' shared `MAX_CL_STATS`, reference guards,
actual enum bit, optional argument, native synchronous fallback and unchanged
mutex release/VM resolver.

| Review contract | Disposition |
| --- | --- |
| Equal-to-count must not reach numeric/string storage | Accepted: all three guards now use the reference exclusive bound; successful result paths remain native. |
| Omitted flags and blocking failure must follow primary | Accepted: zero default and copied failure predicate; cache owner unchanged. |
| CSQC-only numbers and native ownership must remain | Confirmed: 317/330/331/332 and SSQC refusal unchanged; no second cache or VM. |

`git diff --check` passed. Effective reviewer model metadata was not exposed,
so this is a bounded advisory source review, not a certified senior-skill pass.
No builds/tests/compiler/runtime probes/fixtures were run. The final acceptance
cases and broader interface requirements above remain open.
