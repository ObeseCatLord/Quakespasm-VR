# 2.0 migration status

## Baseline and preservation

The `2.0` branch starts directly at vkQuake commit
`4bc898f29073e8aa41069f0e79e3cb5a9eb73afa`, the upstream `master` tip checked
on 2026-09-20. Its initial tree was identical to upstream. Subsequent commits
are migration work. `vkquake-upstream` tracks the original repository at
<https://github.com/Novum/vkQuake>; `origin` remains the project fork.
The separate `quakespasm-2.0` worktree avoids modifying the existing `openxr`
worktree. No history was reset, rewritten, pushed, or deployed.

Behavioral references:

- Inherited base: `8c5a6007a60098b6a5b5c5b552def70e1238a852`.
- Product: `7bc466b594e7a7e584dc47879eb6c00f971b01b1`.
- Migration source: `3080841333fa94000df7e1fb9e549c7158685dd6`.
- The original worktree's 13 uncommitted files were copied with a binary patch
  and SHA-256 manifest into the shared Git directory's local-only
  `migration-references/2.0-30808413/` snapshot. Those files remain in place.
  WIP is preserved evidence, not automatically accepted behavior.

[migration-preservation.csv](migration-preservation.csv) enumerates the entire
inherited tree, the product/development deltas, and the WIP paths: 904 unique
paths. There are 553 committed delta paths at the migration source, one more
than the architecture review's 552 because the reviewed plan was committed
subsequently. Blob IDs distinguish inherited/current/donor content; empty
source IDs include deleted paths. WIP hashes identify the preserved local
snapshot. This is an exhaustive path inventory, **not a completed behavioral
audit**. A `pending behavior mapping` entry must gain an explicit disposition
(reused, adapted, donor equivalent with evidence, or obsolete with reason).
Deleted paths must be reviewed for intentional removals rather than restored
blindly. Unchanged inherited dependencies are included deliberately.

## First source checkpoint

The existing standalone OpenXR backend is adapted to the Vulkan-only donor.
It retains the original session/frame/action/tracker/gaze state machine and
Vulkan attachment API; the legacy OpenGL transport is omitted from this donor
module. This does not remove the requirement to migrate inherited OpenVR
behavior through a Vulkan compositor path.

Meson and the native Visual Studio project compile this module as C++, while
the upstream renderer remains C. The backend uses SDL2/SDL3 loader APIs and
loads OpenXR dynamically. Vendored headers, licensing and exact source pins
are in `Quake/thirdparty/openxr/PROVENANCE.md`.

**This checkpoint does not enable VR gameplay.** Engine initialization,
renderer/device attachment, menu settings, scene submission and shutdown are
not connected yet. Importing the runtime interfaces does not qualify gaze,
foveation, Vulkan stereo, or any headset. No new CLI switch claims otherwise.
Routine builds/runtime tests are deferred until the implementation slice is
complete, as requested. Source review is not build or device validation.

## Next integration gates

Follow the [reviewed architecture plan](vkquake-base-migration-plan.md).
The next bounded end-to-end slice is:

1. Let the existing runtime choose the Vulkan instance/device requirements;
   retain vkQuake's device/resource owners and desktop startup path.
2. Attach OpenXR swapchains to donor rendering. Join actual queue submission,
   not just recording tasks, before releasing images; retire task and GPU users
   before teardown. Add no second render graph or image-lifetime authority.
3. Reuse existing VR view/input/gameplay algorithms for an actual map, weapon,
   movement and HUD. Update gameplay/particles once per logical frame. Preserve
   independent live avatar poses and their shadow poses within donor models.
4. Establish task-enabled opaque multiview with moving brush lighting, restart,
   focus loss and shutdown before expanding the bulk migration. Then qualify
   Linux and native Windows; device testing remains a separate user checkpoint.

Save dialect validation must precede world/game changes: the fork's multiplayer
version 6 conflicts with donor KEX version 6. Prediction, command timing and
server changes are one coupled migration, with explicit peer revision fixtures.
QuakeC callback ownership must remain coherent when donor GUI work uses tasks.
These are prerequisites, not optional cleanups after porting files.

## Runtime and performance requirements

- Linux/Monado/Beyond 2e, Windows headsets, and Steam Frame PC streaming and
  standalone Linux ARM64 remain release targets. No eye provider is assumed.
- Eye tracking is an optional VR-menu toggle, off by default. Missing/invalid
  gaze renders full quality. Fixed foveation is explicit opt-in only, never a
  default or automatic fallback. Runtime support must be detected; the presence
  of a headset name or an extension declaration is not proof of working gaze.
- Reuse donor multithreaded loading/rendering, resource ownership, GPU batching,
  precision and asset-format support. Add conservative two-eye visibility and
  single-pass stereo; foveation must not cull visible peripheral geometry.
- Preserve all inherited/project behavior, including OpenVR, weapon wheel,
  prediction/co-op, saves, QC/mod compatibility, avatars/FBT, physical melee,
  Gorilla movement, akimbo, audio/voice, catalogue and configuration/assets.
- Compare complete frame CPU/GPU time and loading on large maps including
  Mjolnir `mj4m1`. No speedup is claimed before comparable measurements.

## Upstream maintenance

Keep `vkquake-upstream/master` as the untouched donor tracking ref. Merge reviewed
upstream updates into `2.0` using ordinary ancestry; do not replay the old fork's
unrelated history or repeatedly cherry-pick the entire renderer. Keep product
policy at existing narrow boundaries and record donor conflicts by subsystem.
Avoid mass formatting, renaming donor files or replacing its working owners.
Retain source licenses and provenance for each transplanted subsystem. Review
conflicts against both the product behavior reference and the donor's change.
Branch ancestry makes merging possible; it does not promise conflict-free merges.
