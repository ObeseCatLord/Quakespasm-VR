# End-of-implementation Linux and ARM qualification

2026-09-30. Planning only: do not run this pass until surviving implementation
requirements are closed. Earlier source checkpoints and historical test results
do not qualify the current tree. Current user scope decisions override older
acceptance text. This plan does not shrink the185-feature inventory to recently
changed code.

## Entry and evidence ownership

First reconcile every migration-feature-map.csv row against its current
production owner and linked plan. Distinguish source-integrated/native-reused,
concrete implementation gap, software qualification pending and user-excluded.
A matching name, symbol, number, archived comment or absence of TODO is
insufficient. Review the512 MAIN/XR QC interface entries and inherited command/
setting index at their actual dispatch/consumer boundary; intentional native
improvements and retired settings need explicit treatment, not blind aliases.
Do not mark a row complete because a narrow neighboring fixture passes.

Use the existing Meson and native packaging recipes. Build the complete desktop/
OpenXR client and required dedicated components on this Linux machine, then
from a pinned source snapshot in an isolated directory through ssh foundry
on native Linux ARM. Qualify the same voice/codec/Steam Audio feature selection
and generated shaders on both architectures. Inspect linked dependencies and
installation artifacts. A no-SDK fallback build is additional coverage, not a
substitute for the required native audio package. Do not modify a running game/
server or deploy to Foundry. Windows remains a release target with builds deferred.

Reuse existing tests/README.md recipes and native runners where they cover the
current contract. There are119 files under tests at this checkpoint; neither
that count nor an aggregate green result proves scope coverage. Select applicable
cases after reading the actual fixture boundaries and current code. Add only
missing meaningful end-to-end software coverage after implementation. A fake
runtime, packet counter or helper test must keep its proof limitations explicit.

## Full surviving scope matrix

| Family | Required software evidence at the actual owner |
| --- | --- |
| Native base and delivery | Desktop map/menu/console/config and built-in vkQuake desktop demo behavior; build/install/packaging and resource lookup; requested shared changes without replacing native vkQuake ownership; documented upstream adapter provenance and a separate disposable upstream merge rehearsal. No VR demos or additional demo feature gate. |
| OpenXR session and input | Device/runtime qualification, session/frame acquire/wait/release/submission/retirement ordering; startup, compatible attachment, healthy disable and explicit recovery; optional trackers/gaze, focus loss, physical-hand/profile changes and held-key release. Exercise actual backend/renderer paths where software facilities exist; do not call a fake-runtime result headset proof. |
| Stereo and native graphics | Actual two-layer opaque multiview shaders/passes and independent eye cameras; once-per-frame animation/gameplay/particles/QC; native Vulkan MSAA/precision/lighting/warps/transparency, protected UI/weapon detail, shared AO qualities with unchanged desktop AO and stereo VR AO; conservative either-eye PVS/frustum/backface/moved-brush rejection independent of gaze. Rendered software evidence must accompany eligible path coverage. |
| Foveation | FB/META preference and device-time fallback eligibility, exact extension/feature-family ownership, density-map/image lifecycle and MSAA compatibility; gaze toggle, freshness/validity and unavailable eye tracking restore full quality; fixed mode is explicit only, never default/fallback. KHR is a startup/device fallback, never switched live onto an FDM-created device. No quad views. |
| VR movement and presentation | World/floor scale, recenter/roomscale, head/hand aiming, snap/smooth/180, authoritatively aligned camera continuity, crosshair depth/muzzle transforms, inherited HUD/console/scoreboard/menu placement, pointers/sliders/keyboards and mirror/hidden-area behavior. Gesture-only melee suppresses the recognized melee trigger and presentation attack animation while preserving QC damage/timing; ordinary desktop/ranged attack remains. |
| Wheel, calibration and avatars | Stable schema IDs, eligibility/remembered selection and real-action clearing/release, desktop/VR sessions and cancellation, co-op action control, copied profile/preset/global save behavior and one solo/MP muzzle/weapon transform; native generated paired models and haptics, player/avatar/equipment/VRM/FBT endpoint identity and independent per-instance poses/shadows/bounds. |
| Prediction and multiplayer | Real native loopback transport, signon and server-produced snapshots/parser, public desktop/private VR negotiation, command duration/redundancy/ACK/replay/cadence, packet loss/reorder/reconnect/teleport/map/save discontinuities, custom QC movement and CSQC entity/events; mixed desktop/VR play and ordinary source-authorized commands. Header-only synthetic cases cannot replace actual owner round trips. |
| Co-op and saves | Classic opt-out and shared pickup/equipment/keys/ammo policy, callbacks/targets, collision/friendly-fire/telefrag, normal cooldown/respawn-near/wipe/fallback, late join, native co-op actions/outlines/names, autosaves, dialect-discriminated v5/v6/v7 saves/pending clients/hubs/dead inventory; passive serialization and optional QBJ3 limbo/color/model cleanup with cancellation. Revival excluded. |
| QuakeC/mod compatibility | Both VM permission/slot/name/capability/argument/return/error contracts; actual program execution through native registries, strings/buffers/files/search/named calls/reflection/model and entity queries/message reads/CSQC drawing/input/events/localization; simultaneous VM resource ownership and reload/abort lifetimes. Preserve deliberate native data-path/capacity policies. Literal registration coverage alone is not proof. |
| Assets, maps and performance mechanisms | Actual PNG/TGA/JPG/JPEG/fullbright MDL/MD3/MD5/static/level loads, WAD3 per-texture palettes, native liquid lightmaps and CPU/GPU lightstyle interpolation, material/texture-cache/recolor identity, precision/clip bias; native worker rendering/loading, large-BSP/no-VIS/visibility/allocation behavior, alias/BLAS batching and scripted particle lifetimes/effects. Source mechanisms are required; measured speedups are not claimed. |
| Audio and discovery/UI | Actual optional Opus packet/jitter/generation/levels/mute owners and budgets, game/voice HRTF/native fallback/radio/occlusion/reverb/local wet reflections; system-default mic, saved VR default-on opt-out versus desktop opt-in; capture error/profile transitions, meter/HUD/PTT. Steam/rerelease/localization paths, mod launch/filter/catalogue/approval/validated package cancellation, reconnect and UI scale/branding. |

The inherited aim source repairs add focused cases to the VR movement and
presentation row: deadzone 0/70/out-of-range/nonfinite and unchanged valid
fractional values; controller-to-head/mouse/blended history with unavailable
hands, rejected exits during HMD loss, locks, centerview and absolute/relative
corrections; no retired VR seed
on a desktop setting change; pitched/rolled native intermission entities with
one physical-head contribution and unchanged desktop cameras. Native co-op
results, deathmatch scores and finale use the existing tracked intermission
panel. CSQC score/death/intermission canvas placement is now source-integrated
through the [existing canvas adapter](csqc-score-panels-2.0-plan.md). Final
qualification must cover mixed native inventory/voice, registered callback
combinations, source clipping, independent framebuffer scaling and native
intermission fallback after QC errors; source acceptance is not rendered proof.

The optional inventory stat repair adds actual registered-QC/server/client/wheel
cases to prediction/multiplayer and wheel qualification: representable bit31
and upper unsigned32 masks, ordinary fractions/negative values, invalid or
wrong-type fields, valid zero versus moditems/items_dwell fallback, both items2
packing paths and unchanged native custom-stat precedence. Packed STAT_ITEMS
retains the low nine items2 bits; STAT_VR_ITEMS2 retains the normalized full mask.
Keep ordinary core field values valid: this is a bounded optional-field repair,
not a complete native numeric-conversion audit.

Read game assets only from the existing quakespasm_straight installation. Native
runners may link/copy needed data into disposable writable profiles; saves,
configs and generated QC programs belong there. The map/mod matrix includes
stock, AD/ad_tears, q30a1024 and ordinary QBJ3/Enyo/Dwell/Mjolnir, mj4m1,
tershib/shib1_drake, peril/tavistock, large BSP2/no-VIS and original-level
z-fighting examples. Record absent assets as missing evidence, never as success.
No Mjolnir dual-state extension, mod-by-mod ability framework or performance
benchmark is required.

## Exit and final review

Record exact source revision, build options, platform, selected cases, results
and coverage limits per requirement. Correct failures at native owners, then
rerun affected coverage; do not repeatedly broaden green testing without cause.
After the full pass, use the requested local Astra senior review on the verified
implementation/evidence brief, spot-check its findings and record dispositions.
If effective settings cannot be verified, label source advice rather than
certification. Resolve required findings before claiming implementation complete.

User live headset/eye tracking/multiplayer/performance trials and Windows builds
remain outside this pass. General live incompatible VkDevice/device-loss rebuild,
skyrooms, quad views, Gorilla/swim propulsion, instant stop, physical-contact
melee/parry, Mjolnir hybrids, imagedump, revival, VR demos and additional demo
features do not become hidden gates.
Within surviving software scope, missing/indirect evidence remains open. Keep
the full migration goal active until its completion audit is supported.
