# Weapon-wheel session policy follow-up

2026-09-30. WPN-002 and WPN-011. Verified2.0 base a5012ee8; primary
master51b452c0. Source comparison only; all runtime checks remain end-of-goal.
This follow-up preserves catalog/schema/renderer owners and adds no game policy.

## Demonstrated gaps and reusable owners

Primary vr.c:12250..12292 remembers a catalog identity across no-hit frames and
resolves it against current visible/selectable entries again on release. A real
action hit clears that remembered weapon; save/load/teleport are never retained.
Primary begin/end clear the session. Current vr_weapon_menu.c:2778..2811 clears
hover every frame and returns immediately when no panel hit exists; release at
2831 requires pointer_valid. Stable row IDs already exist and should replace the
primary's array ordinal. Current BuildVisible/EntrySelectable retain inventory,
ammo and descriptor authority; no duplicate selection rules are needed.

Current gl_screen.c:2011..2039 merges no panel intersection and blocked panel
into pointer_valid=false, although a finite tracked controller ray exists for
both. XR/focus/reference loss already cancels the whole session. Retention must
not reinterpret lost tracking as a valid release or retain an action. The
playspace path still needs target visibility for retained meshes; use existing
prepared target geometry/world trace, with no new renderer or occlusion cache.

Primary vr.c:2164 defines archived vr_weaponmenu_player_teleport=1 and uses it
for both the teammate list and spawn action. Current CoopPlayersAvailable at
1893 lacks that control. Existing menu cvar registration and common action
eligibility are the narrow reusable boundaries. Server teleport authorization
remains authoritative. This control is functionality, not a legacy-setting alias.

## Minimal adapters and sequence

1. Restore the literal archived default-on player-teleport control at existing
client input registration; apply it at the single CoopPlayersAvailable predicate.
Action rebuild/release/hover validation already share that predicate, so disabling
it removes both actions and prevents a prepared stale action from committing.
Ownership: vr_weapon_menu.c/.h and only registration in cl_input.c. Estimate
8..20 net lines; no new options page or server policy. Source-reviewed literal
copy is sufficient; runtime enable/disable/release checks remain deferred.
2. Before implementing retained-selection semantics, obtain bounded local Astra
source advice on main-thread update ownership, panel-miss versus tracking failure,
playspace target visibility and desktop behavior. Lean: one remembered stable ID
in the existing session, live eligibility resolution, and shared selection update
before draw workers. Real actions clear it. Invalid inventory/visibility disables
selection without changing identity; cancellation/game reset clears both.
Consider merely keeping previous hover when pointer_valid as the smallest edit,
but reject it if off-panel misses or action transitions still differ from the
reference. Do not create a second session/catalog or mutate selection from workers.
Expected ownership vr_weapon_menu.c/.h and narrow gl_screen.c setup/pointer seam;
80..160 net lines. Reopen above200, another state owner, or expanded rendering.

Final Linux/ARM checks cover hit then blank/off-panel release, transient model/
stat disappearance and return, action then miss (no action and no resurrected
weapon), unowned/empty/unavailable entry, occluded target, tracking/focus loss,
mode/map/game cancellation, stable schema IDs and unchanged desktop actions.
No runtime/headset/performance claim is made by this plan.


## Player-action control source checkpoint

The7-net-line production adapter copies the primary's archived default1 control,
registers it in existing CL_InitInput, and checks it in the common co-op action
predicate. Nonfinite values conservatively disable optional actions. Both player
and spawn actions, rebuilt release candidates and prepared hover validation use
that predicate. Main compared those consumers and the primary declaration;
scoped git diff --check passes. No tests/builds ran. Retained selection remains
unimplemented and requires the separately requested source disposition.


## Adopted local Astra source disposition for retained selection

Requested local Astra/max verified source and corrected its initial task-fence
recommendation after main challenged the need for a scheduling change. Effective
settings remain unavailable via the tool, so this is source advice, not formal
review certification. Main spot-checked setup/task edges, resident frame copying,
SDL cursor access and action/selection consumers.

| Recommendation | Disposition |
| --- | --- |
| Force wheel-open setup to main with an early join | Reject after source reassessment: no demonstrated incompatibility requires moving the existing writer. Keep SCR_SetupFrame after R_PrepareStereoFrame; existing setup->before_mark/GUI/scene dependencies publish one selection/frame. No early join, task resubmission or lost draw parallelism. |
| SDL mouse/window APIs require main affinity | Adopt: sample/convert desktop cursor once on main before task submission, then let existing setup consume that snapshot. Draw workers read prepared coordinates/selection, never query SDL. Main-thread release may sample again and use the same resolver. |
| Remember identity separately from effective selection | Adopt one stable catalog ID in the existing session. Weapon hits remember it; real actions clear it. Miss resolves current BuildVisible/EntrySelectable eligibility; temporary absence publishes none but preserves remembered identity. Cancel/begin/reset clears identity. |
| Panel misses, obstruction and tracking loss differ | Adapt at existing pointer/target seam: invalid tracking/focus/reference still cancels; blocked projection cannot create an action. Retained weapon eligibility additionally checks its existing playspace target geometry/visibility and distance, including model-less labels; no visibility cache. Keep this explicit safety improvement over the donor's eligibility-only retained fallback. |
| Drawing and release must share the published result | Adopt existing prepared frame for desktop/VR drawing and stable-ID/live-eligibility release. VR fallback requires valid session/tracking and matching prepared selection, not panel intersection. Actual actions still require real hit, slot/name and policy validation; action->miss cannot resurrect a weapon. |
| Desktop window focus lacks cancellation | Adopt one desktop-only VID_FocusLost cancellation hook. XR keeps runtime focus authority. |
| Haptics need an early main-thread setup fence | Reject as unsupported by inspected source; current setup already owns selection haptics. Keep effective nonempty-ID transitions there. A future demonstrated runtime affinity requirement can use a bounded transition at the existing final join. |

Revised exact production ownership: vr_weapon_menu.c/.h, narrow gl_screen.c
cursor/setup/pointer seams, and only desktop-focus cancellation in gl_vidsdl.c.
Budget120..200 net lines; reopen above200, a second session/selection owner or
render scheduling rewrite. Keep native catalog/material/frame/task owners and
current submission sequence. No tests/builds/probes before full implementation.

## Retained-selection production source checkpoint

The four-file slice is110 net production lines. Main reviewed the complete
patch against primary vr.c:12250..12292, current action validation, prepared
frame copying, native IN_GetMousePos scaling and task submission/dependencies.
One remembered stable ID is separate from effective hover. Weapon hits store
it; real actions clear it. Misses re-resolve prepared live eligibility and
playspace target distance/world visibility. Temporary absence publishes none
without discarding the remembered identity. Existing session cancellation,
tracking/reference/map/game loss and begin/reset clear the session; a changed
VR wheel mode now cancels too.

Desktop SDL cursor sampling stays on main and uses existing video-to-render
coordinate conversion. SCR_SetupFrame consumes a copied12-byte task payload
after R_PrepareStereoFrame; all existing setup/scene/GUI task edges remain.
Draw workers use the prepared effective selection and coordinates. Desktop
release may sample again on main; VR release requires valid tracking/session,
matching prepared generation and live stable-ID eligibility. Actions keep
actual-hit and slot/name/policy validation; they cannot be retained. The archived
player-teleport toggle remains authoritative. Existing selection haptics stay
in setup with no early fence or scheduling rewrite.

Scoped git diff --check passes. No tests/builds/compiler runs/probes/fixtures or
benchmarks were performed. Final Linux/ARM software and user live VR acceptance
remain pending; source integration does not certify those outcomes.
