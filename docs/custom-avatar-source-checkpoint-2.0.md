# Custom avatar source checkpoint

2026-09-30. Current source inspection for AV-004/005, with the existing
AV-008 calibrated-humanoid adapter identified below. Baseline `1511549c`;
reference `51b452c018273647dcf94f4628a370267ff8fa91` in the read-only
QuakeSpasm OpenVR checkout. No build, fixture, probe or device trial was run.

## Reference reuse and actual dispatch

A bounded local worker compared `custom_avatar.[ch]` and `player_avatar.[ch]`
against the pinned primary: all four are unchanged. It inspected package
admission, identity producers, server relay and client descriptor consumers.
Main separately followed startup, outgoing commands, server handling and the
native model/frame boundaries that were outside the worker's claim.

`custom_avatar.c:CA_ParseManifest/CA_ReadPackage/CA_Consider` retain strict
version-1 manifests, quoted names, bounded optional scale/equipment/bone mappings,
duplicate/unknown-token rejection and fixed required manifest/mesh/skin files.
Glow is optional; file-size/TGA admission remains bounded. `CA_Hash` includes
the fixed names, lengths, contents and missing-file markers in the inherited
SHA-256 identity. `CustomAvatar_Init` retains startup-only scanning, root
priority, sorting/local IDs and dedicated-server exclusion. `CL_Init` actually
calls it before registering and validating `cl_avatar`. `menu.c` selects that
same cvar through the native player setup page.

`player_avatar.c` retains portable custom keys, builtin/reserved-name exclusion,
canonical numeric parsing and exactly 64 lowercase hexadecimal digest digits.
`cl_main.c:CL_TrySendAvatarCapability/CL_TrySendCustomAvatarCapability` append
capabilities only after the matching offer and reliable-buffer admission.
`CL_TrySendAvatarSelection` uses local registry key/digest, queues a Ranger
numeric selection until custom capability is available and resends the custom
choice when that capability is admitted. Buffer pressure leaves a pending
selection for retry; it does not publish an arbitrary remote filesystem path.

`sv_user.c:SV_HandleAvatarCapability/SV_HandleCustomAvatarCapability` gate the
existing string-command handlers. `SV_HandleCustomAvatarSet` validates key and
digest, stores them with Ranger numeric projection and calls the existing
slot broadcast. `sv_main.c:SV_FlushAvatarSlots` emits numeric projection and
custom descriptor together for capable recipients, retaining pending slots
when they do not fit. Unsupported recipients retain ordinary presentation.
`CL_ParseCustomAvatarSlot` parses offered descriptors, resolves both local key
and digest on each resend, and selects Ranger on absence/mismatch. Warnings
are deduplicated; reliable numeric/clear updates retain their existing owner.

## Vulkan admission and render consumer

`Mod_GetAvatarCustomModel` uses the native `mod_known` and model resource
owners. Admission rereads the fixed package snapshot through
`CustomAvatar_ReadData`, which checks its digest against startup identity.
Already-admitted immutable models are reused each frame; no per-frame package
scan or rehash is added. A changed or failed package returns unavailable.

The native MD5 parser's custom boundary validates skeleton hierarchy/names,
manifest rig compatibility, fixed `skin` material, vertex/triangle/weight
bounds, uniqueness and finite coordinates. `Mod_LoadCustomAvatarSkin` submits
the admitted fixed skin/optional glow to the existing Vulkan texture owner.
Failed model admission frees the snapshot and native model/mesh resources and
marks the local package failed. Registry admission alone is not mesh acceptance.

`r_vrik_render.c:R_VRIKRenderStageAvatar` resolves the manifest profile and
native-admitted model before dependent recording, clearing any previous staged
choice on failed admission. It validates the Ranger source, target skeleton,
surface/joint agreement, presentation and floor correction, then publishes
staged views with their borrowed pointers rebased. The existing alternate
candidate produces one frame-owned palette and shared presentation for body,
equipment, conservative bounds and shadows. This keeps authoritative network
entities and vkQuake GPU skinning intact.

## Calibrated humanoids and experiment boundary

The existing [calibrated humanoid plan and source dispositions](avatar-humanoid-vulkan-port.md)
remain the AV-008 contract. Actual `r_vrik_render.c` staging gates
`r_avatar_humanoid` on a local custom package, caches the admitted bind/profile
mapping and supports generic mode 0, calibrated target-length IK mode 1 and
calibrated mode 2 without that IK. Missing optional reference data returns to
generic retargeting. The frame candidate applies inherited
`R_AvatarRetargetHumanoid` and tracked raw-goal or desktop support-hand solves
before the common prop/presentation consumer. This identifies implementation,
not measured CPU cost or asset-backed visual correctness.

AV-009 is a preservation experiment, not general VRM release support. The
pinned reference retains `Quake/r_alicia_spike.[ch]`,
`experiments/alicia/` and `docs/humanoid-retargeting.md`. Its Makefile includes
the direct OpenGL/CPU-skinned sample only under `USE_ALICIA_SPIKE=1`; loading
also requires the fingerprinted `-aliciavrm` asset and default-off controls.
The [existing Vulkan review](avatar-vulkan-implementation-review.md) and
humanoid plan explain why it is a private comparison reference rather than
another production renderer. Preserve that provenance/opt-in distinction;
this checkpoint does not claim the experiment has a Vulkan importer or that
its remaining general feet/torso/finger work has been implemented.

## Remaining qualification

The final Linux/ARM pass still requires actual startup/root precedence,
capability and selection retry, signon/late-join/slot-reuse relay, same-key
different-digest/missing/changed package fallback, malformed mesh/rig/texture
admission, reload/resource retirement and independent selected-player
draw/equipment/shadow/bounds. Exercise custom humanoid modes 0/1/2, incomplete
rig fallback, unreachable targets and tracked versus desktop animation.
No new package or calibrated-humanoid implementation gap was demonstrated by
this bounded inspection. Source integration is not rendered/network proof;
the full migration goal and final qualification remain open.
