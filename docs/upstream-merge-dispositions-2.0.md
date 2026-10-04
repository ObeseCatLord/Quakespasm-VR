# vkQuake merge dispositions and source receipt

Date: 2026-10-04. Scope: ordinary upstream history merge into the existing
native and VR engine; isolated implementation only.

## Ancestry and review provenance

- First parent: `7cc8cdb20db55f550addf1b11b71ec4bbd93b8c6`.
- Second parent: `749b4fd43fe8b8554b34163dc9469c4a08a4def1`.
- Merge base: `d336dbae0dc21b4693d34052c194ca6a32602e08`.
- Incoming history: 36 commits, 50 paths, 46 paths changed on both sides.
- Recorded conflict tree: `b34667b57e050488029f3eae62456250f13f1d7e`;
  17 conflicted paths and 55 exact conflict-start markers. The original
  plan's 47/65 counts were corrected from actual Git objects.
- Astra primary review effective model/effort: **gpt-6-astra/xhigh**.
  Main independently spot-checked the load-bearing save, shader and signon
  findings and approved all coupled dispositions.

The in-progress merge and five existing partial resolutions were preserved.
No whole-tree ours/theirs resolution, history restart, source delegation or
production writes were used. The pre-merge plan was recovered by reading its
stashed path, without popping or applying the stash.

## Approved dispositions and implementation

| Boundary | Decision and resulting behavior |
| --- | --- |
| Save purpose and notification | Separate manual, single-player autosave and co-op autosave purpose from notification suppression. Quiet manual saves cannot advance co-op rotation or divide by a zero slot count. |
| Snapshot and completion | Retain immutable main-thread serialization, two bounded SDL worker slots, atomic replacement and main-thread poll/drain. Only successful replacements publish the catalogue, sequence-eligible last-save state and purpose-specific completion. Workers never access live QC state. |
| Autosave scheduler | One frame entry dispatches native SP scoring/safety or existing co-op policy. Busy admission and SP I/O failure back off. SP time/cheat updates occur on successful completion. Existing co-op v7 records and load restoration remain owned by the native path. |
| Desktop save exception | Preserve accepted requests instead of upstream newest-request cancellation; reject same-target or full-slot admission. Exclude the upstream borrowed-VM serializer and alternate worker graph. |
| Remote signon | Retain split native chunks and reservation calls. Metadata shares the receiver budget. Admit at most one remote chunk per retained outgoing message; exact-fit chunks and the two-byte terminal marker are admitted independently. |
| Blocked signon lifetime | Once a remote chunk is admitted, retain the 32,000-byte `message.maxsize` envelope through blocked frames and `PRESPAWN_FLUSH`, including ordinary reliable writers and avatar offers. Restore its previous capacity only after a positive transport send. Check overflow and size at the actual sender and fail visibly. A repeated prespawn request cannot reset a retained chunk. No generic QC fragmentation is added. |
| Metadata ordering | Server metadata drains before signon2; user metadata drains before signon3. Preserve admission/retry state and native local/remote distinctions. |
| AO sampling | Adopt upstream quality-specialized desktop AO, ceil-half top-left sampling, lookup and mismatch skipping. Eye, resolution and quality remain separate axes; retain VR resolution controls, asymmetric projection, stereo shaders, multiview and per-eye resources. Remove obsolete half-mip reconstruction state. |
| AO fallback and resources | Implement mismatch reduction with shared memory/barriers for SHARED_MIPS, keeping the subgroup path. Update seven-binding producer, two-binding lookup, specialized pipelines, pool budgets and teardown together. Hilbert data remains shared; depth/AO/tag resources address the correct eye. |
| Menus and input | Consolidate native preview state while preserving categorized graphics rows, 24-mod browser, tracked panels/pointer controls and VR drawing. Desktop ui_mouse gating does not suppress tracked input. |
| VM and movement | Reuse upstream SetModelSize behavior and adapt water overrides at all guarded PostThink sites. Preserve private head/roomscale scopes, current prediction and existing command ownership. |
| Model, sky and platform | Combine bounded copied-name termination with unnamed texture fallback; clear sky cache before model destruction. Keep canonical filesystem wrappers and existing strong Windows atomic-save replacement. |
| Candidate versus qualification | A strict compiled merge object is a reversible integration base. Dedicated acceptance workers and main perform behavior qualification at implementation end, before shipping. |

All incoming paths were audited, including clean merges in client/demo,
console, image, input, sound, view/world, model, sky, feature/sampler state,
lightmap shaders, VM commands/extensions and Windows platform code. The two
incoming texture-manager assertions are retained; independently owned
rectangular-mipmap work is not implemented here.

## Narrow compatibility changes beyond the incoming path set

- `Quake/common.make`: link the four retained SHARED_MIPS shader variants
  referenced by the native pipeline selector.
- `tests/local_load_native_fixture.c`: use the unified autosave entry point.
- `tests/metadata_publication_native_fixture.c`: adapt signon pointer access.
- `tests/ssao_shared_mip_vulkan_fixture.c`: supply the seventh mismatch-tag
  binding, initialization and cleanup; retain the existing four cases.

The background-save fixture is unchanged from the first parent. Additional
save witnesses and the separate draft signon fixture were removed from this
merge; independent acceptance owners implement further cases in their files.
Bonk and texture implementation remain independent follow-up work.

## Verification and remaining acceptance

- Strict isolated native Make DEBUG=1 / USE_SDL3=1 / -j4 compilation passed
  after completing implementation, including the final signon lifetime fix.
- The save dialect fixture passed 51 cases.
- The existing shared-mip GPU fixture passed its four even/odd/partial cases
  over all five mips with maximum one ULP error.
- All 24 compiled SSAO variants passed SPIR-V validation for Vulkan 1.1.
  Disassembly of all four shared variants contained no subgroup operations
  or subgroup capabilities.
- Conflict markers, unmerged index entries, staged whitespace and exact
  two-parent ancestry are checked at merge finalization.

The bounded shader/dialect results cover their stated fixtures; they do not
establish full native save/load, remote playable signon or headset behavior.
The final signon correction has strict compilation evidence; blocked-send
behavior is assigned to the independently owned metadata acceptance fixture.
Additional save admission/failure/completion/load witnesses and seven-binding
GPU acceptance are independently owned. Final consolidated checks run after
Bonk implementation against the exact integrated candidate. Windows runtime,
physical headset visuals and XR reattachment remain unqualified here.

No external push, deployment or production shipping accompanies this receipt.
