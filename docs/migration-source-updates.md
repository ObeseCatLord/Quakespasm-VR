# Source updates after `1327f795`

This inventory extends the product-source evidence from
`1327f795cc2e3a8e4f7c9d68e31d64383930cc00` through exactly two descendant
commits: `c1b5f2ab0d458b08470d886240faba7801c981ae` and
`2857e8b95b6ad8eeaeabdfdfd79b55e5f5645869`. These rows are pending migration
evidence only. They do not claim that branch 2.0 implements or accepts the
source behavior.

The preservation ledger keeps its 905-path schema and all inherited, donor,
and WIP evidence. Only the seven paths changed by `1327f795..2857e8b9` advance
their `current_master_blob` to the final product pin and set
`new_master_delta=1`:

| Path | `1327f795` blob | `2857e8b9` blob |
| --- | --- | --- |
| `Quake/q_sound.h` | `c3de0c7f57e6ec71df1291935be6912dfe2f6946` | `9ad35c83fa167755e95c877ffb079bf8f2c7d003` |
| `Quake/sbar.c` | `996456febe2aed51016692944fff9116a4220664` | `94f11f9dc15dada535984684b4a3d5a68ebab039` |
| `Quake/snd_dma.c` | `120d8af01406c3bfcc8b03bac8c94e9014247631` | `46cb34a92cdab8899a51a5fd8970e6a6ec0f1723` |
| `Quake/sv_main.c` | `8eec9d310373feb96e59cd517e491a500480d30c` | `73455547b5f770bc9886d297d7471d1a63a50cd6` |
| `Quake/vr.c` | `c494894c556140b5e983bb3e7b555639c4c74316` | `ef4c556fa5e29b2c140cb403525f2966f526ff04` |
| `Quake/vr.h` | `dffcd4c6b10516b10d37b523a409e0a789a16808` | `95d36e5bce34e91b558f76cde30ef5320f105c61` |
| `Quake/vr_weapon_catalog.h` | `0137d0e0bad61832f4f40a7be08b4cbb518db9ad` | `720e44574b4f4fd885a501a0647d8b3bd9ea7581` |

Concrete pending mappings:

- **Spatial ambience/channel allocation** — `c1b5f2ab`,
  `Quake/q_sound.h` blob `9ad35c83fa167755e95c877ffb079bf8f2c7d003`
  line 115 and `Quake/snd_dma.c` blob
  `46cb34a92cdab8899a51a5fd8970e6a6ec0f1723` lines 356, 368, 399 and 506.
  The source classifies incoming audibility and ranks active spatial channels
  before replacement. Route to existing `AUDIO-006` and `AUDIO-011`.
- **Weapon catalog identity/model matching** — `c1b5f2ab`,
  `Quake/vr_weapon_catalog.h` blob
  `720e44574b4f4fd885a501a0647d8b3bd9ea7581` line 62 and `Quake/vr.c` blob
  `ef4c556fa5e29b2c140cb403525f2966f526ff04` lines 1245 and 1260. The source
  treats matching `g_`/`v_` model basenames as one catalog identity. Route to
  existing `WPN-002` and `WPN-003`.
- **VR HUD spacing for AD CSQC layout 4/104** — `c1b5f2ab`, `Quake/sbar.c`
  blob `94f11f9dc15dada535984684b4a3d5a68ebab039` lines 1309, 1320, 1403 and
  1494, plus `Quake/vr.c` blob `ef4c556fa5e29b2c140cb403525f2966f526ff04`
  line 10531 and `Quake/vr.h` blob `95d36e5bce34e91b558f76cde30ef5320f105c61`
  line 142. Route to existing `VR-012` and `MOD-002`.
- **VRIK map-transition stream reset** — `2857e8b9`, `Quake/sv_main.c` blob
  `73455547b5f770bc9886d297d7471d1a63a50cd6` lines 639 and 5295. The source
  clears accepted-pose and recipient relay sequence/generation state when a
  server spawns a new map. Route to existing `AV-001` and `NET-026`.

`migration-interface-index.csv` is intentionally limited to literal cvar and
command declarations, so the new internal helpers/signature changes do not add
interface rows. The minimum update is to move the four existing MAIN command
line pins shifted by these insertions: `+showscores`/`-showscores` to 297/298
and `vr_migrate_movement_defaults`/`netdiag` to 1182/1185. No broad interface
regeneration is required.

## 2.0 reconciliation checkpoint

The four deltas above now have implementation anchors on branch `2.0`:

- `SND_ChannelAudible` and ranked spatial channel selection in
  `Quake/snd_dma.c` (`6092a1eb`).
- `VR_WeaponCatalog_ModelPathsMatch` in `Quake/vr_weapon_catalog.h`, used by
  `Quake/vr_weapon_menu.c`.
- AD CSQC layout 4/104 handling in `Quake/sbar.c`.
- `SV_ResetVRIKMapState` on map spawn in `Quake/sv_main.c`.

These anchors close the source-delta routing gap, not runtime parity. Linux
builds cover the audio channel change; headset/gameplay qualification is left
to the user after implementation.

## Later product updates through `eb5e048d`

Product `master` advanced by two more commits on 2026-09-25:
`cefb937d0f552ef0c60f72f735916cc023b6676d` (stale world-model
references on game switches) and
`eb5e048d6a9d82b223b03bceb1872539efa3a040` (anisotropic filtering
control). They change five existing ledger paths: `Quake/common.c`,
`Quake/gl_rmisc.c`, `Quake/gl_texmgr.c`, `Quake/gl_vidsdl.c`, and
`Quake/r_world.c`. Their exact `master` blobs are recorded in the preservation
ledger; no new path or literal cvar/command declaration was added. The source
history index contains both commits. The original `1327f795` feature anchors
remain pinned, while these later changes are explicit follow-up evidence.

The 2.0 adaptation in `538eae56` clears the old client model references before
vkQuake's `Mod_ResetAll` and resets cached view leaves in `R_NewGame`. The
source's `R_PrepareVRStereoVisibility` guard is not copied because vkQuake's
stereo PVS runs through `R_MarkSurfacesPrepare` after its regular scene setup;
that source function does not exist here. Both Linux builds pass. A live
switch/reconnect check still needs to establish loading-screen and first-frame
behavior.

The Vulkan renderer already exposed `vid_anisotropic` as off/on at the device
maximum. Its adaptation keeps `0` off and the saved value `1` at that maximum;
values above `1` request a level capped to device support. The existing
graphics-menu row cycles available levels, and the existing sampler/texture
descriptor owner applies them. Both Linux builds pass. A local windowed probe
stopped at SDL video initialization under the sandbox before sampler creation,
so runtime filter changes and the unsupported-feature path are unqualified.

## Current primary reference `51b452c0`

The read-only product checkout now points to primary branch `master` at
`51b452c018273647dcf94f4628a370267ff8fa91`; no local `main` ref exists.
The feature-map anchors and inherited/donor/WIP ledger columns remain historical
evidence. The ledger's current-primary blobs now advance only the two changed
existing paths, `Quake/vr.c` and `Quake/vr_weapon_catalog.h`; the new
`docs/vr-weapon-wheel.md` adds one row (905 to906) with the existing CSV schema.
The history index records this exact commit. This scoped update does not claim
that every later behavior has been migrated or qualified.

The latest commit, **Fix weapon wheel identity and native mod rosters**, changes
`Quake/vr.c`, `Quake/vr_weapon_catalog.h` and `docs/vr-weapon-wheel.md`.
Directly inspected behaviors to reconcile at existing `2.0`
`vr_weapon_menu.c` / shared-schema owners:

- `VR_WeaponCatalog_IdentitiesCompatible` treats selector, explicit ownership
  and active descriptors as identity evidence. Shared commands or coincident
  preview models cannot merge conflicting explicit identities.
- Held-model identity is distinct from wheel preview. Explicit file fields
  preserve provenance and take precedence over native roster/profile fallback;
  runtime discovery must not replace declared preview geometry.
- Partial calibration/schema overlays enrich known records; own-game
  `roster complete` and verified complete built-in profiles suppress unrelated
  stock guesses. The inherited roster must not define another game's inventory.
- AD and Enyo native upgrades replace parent-slot previews rather than add
  independently owned slots. Observation and ambiguous matching do not invent
  commands, arbitrary inventory masks or first-match overrides.
- Stock shotgun/super-shotgun preview paths are corrected to the actual native
  models. `2.0` now copies those two stock paths and separates exact stock held
  identity at existing active/discovery and main-thread fallback consumers;
  no selection, entity model or native gameplay owner changes.

The current `2.0` wheel has existing provenance, stable IDs, own-game wwheel
filtering and field-preserving schema adapters. Reuse those; do not transplant
the monolithic OpenGL wheel or create another catalog. The latest primary's
identity helper and distinct held/preview identity now have source-integrated
adapters with final local Astra acceptance. Own-game complete-roster metadata
also passes final Astra source review, including calibration-save isolation of
inherited authority. Native upgrade behavior remains **pending reconciliation**.
The [bounded wheel-delta plan](migration-wheel-primary-delta-2.0-plan.md)
precedes adaptation; complete-roster metadata has an Astra design disposition,
retaining desktop/VR wheel parity, the current renderer and all newer melee
profile fields. Stock paths plus their required held-identity adapter are
source-integrated with final source review accepted. Catalog matching/partial
overlays preserve authored raw fields and IDs, reject ambiguity, and learn held
identity independently of preview. Native rosters/upgrades and diagnostic work
remain planned. Builds and presentation
checks are deferred until the full implementation is finished.

Separately, the [gesture-only plan](migration-gesture-only-melee-2.0-plan.md)
directly compares this same primary revision's
`VR_ImmersiveMeleeSuppressTrigger`, final client attack-bit suppression and
`VR_DrawTrackedViewModel` ready-pose presentation. That control/presentation
slice is implemented and source-reviewed; physical-contact damage and native
hybrid adapters remain user-deferred. The
[device reconstruction brief](openxr-device-reconstruction-2.0-plan.md) also
references this revision's actual texture/model restart code rather than
assuming the old vkQuake restart changes devices.
