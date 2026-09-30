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
