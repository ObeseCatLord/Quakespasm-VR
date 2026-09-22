# QSS-M PMove baseline migration

## Reference and scope

Generic player movement in this slice is based on QSS-M commit
`03a498aabc411e2e739adc815c5536b161b9626e`. The VR wire/command reference is
quakespasm-openvr commit `1327f795cc2e3a8e4f7c9d68e31d64383930cc00`.

This slice changes only the staged PM implementation and its focused fixture. It
does not add `pmove.c` to a production build list and does not activate client
prediction. `World_AddEntsToPmove`, PMCL/PMSV glue, and `CL_TraceWeapon` remain
where the branch currently stages them; placement alone is not treated as an
ownership defect.

## QSS-M generic solver baseline

The generic movement reference is QSS-M for
slide/step movement, friction and acceleration, walking/air movement, ladders,
jumping, water categorization, nudging, spectators, and the normal single-step
`PM_PlayerMove` path, subject to the explicit adaptations below. The deliberate
`vr_active` ladder specialization remains:
it uses command-yaw ladder axes from the pinned OpenVR source,
while the `else` branch remains the pinned QSS-M ladder behavior.

`msec == 0` deliberately selects the QSS-M-style single-step path. The fixture
checks this against the actual pinned QSS-M code: with its fixture movevars and
floor hull, ten 100 ms forward commands end at `x=320`, `vx=320`, `z=24`; the
first jump command leaves `z=24` and sets `vz=270`, with translation starting on
the following command.

## Retained non-QSS differences

| Difference | Source / reason |
| --- | --- |
| Donor hull adapter (`PM_InitBoxHull`, transformed hull tests, point/box contents, `PM_PlayerTrace`, `PM_LastTraceEntNum`) | QSS-M keeps these in `pmovetst.c`; this branch uses vkQuake's hull trace signature and content-mask API. The adapter preserves QSS movement callers without rewriting donor collision. |
| `physent_t.modelindex` | Pinned VR Gorilla contacts need brush model identity so a replayed planted-hand contact cannot silently bind to a replaced brush entity. |
| Gorilla state fields in `playermove_t` and raw/trusted Gorilla processing | Pinned VR command behavior. Trusted authored motion carries a generation and reuses body displacement/impulse only when the caller has admitted it; raw input uses the shared Gorilla solver. |
| Gorilla ground/gravity exceptions | Required only while a valid Gorilla command is active. They preserve small hand-created lifts/braces that QSS's ordinary ground snap would erase. |
| VR room-scale displacement | Pinned VR behavior. It is collision-tested as external horizontal displacement, rejects tracker outliers, restores locomotion velocity, and is suppressed for `PM_DEAD`, `PM_NONE`, and `PM_FREEZE`. |
| VR ladder specialization | Pinned OpenVR behavior. `vr_active` ignores headset pitch when deriving ladder forward/right axes and adds forward input as vertical climb; non-VR commands continue through the QSS-M ladder branch. |
| Explicit-duration substeps | Pinned VR/private-wire behavior. Commands carrying `msec` are subdivided to bound integration; room-scale and Gorilla preparation run only on the first substep so physical tracking displacement is consumed once per command. `pmove.cmd` is restored afterwards. |
| Explicit-duration touch deduplication | Keep one impact per touched entity for private commands (`msec != 0`), including across substeps. Legacy commands retain QSS-M's adjacent-repeat suppression and preserve A→B→A touches and their recorded velocities. |
| Safe-origin recovery | Keep the validated `pos` in `PM_TestPlayerPosition`, instead of QSS-M's `pmove.origin`. During nudge fallback those differ; copying the failed candidate would overwrite the valid recovery location. This is a deliberate correctness fix, not merely a hull-signature adaptation. |
| VR swim/jump adjustments | Retained from the pinned VR source: VR jump input supplies minimum swim upmove and scales the waterjump launch by the admitted VR jump speed. These now require `cmd.vr_active`; a generic mod that merely raises `jumpspeed` no longer enters VR behavior. |
| `CL_TraceWeapon` | Existing staged read-only weapon scene query; it uses the donor hull adapter and is outside generic locomotion ownership. |
| `World_AddEntsToPmove`, PMCL/PMSV/PF glue, moveflag packing | Existing branch staging needed by the eventual prediction/server PM closure. The APIs and placement are preserved in this slice as requested. |
| Header include guard, content-mask guards, `trace_t` spelling, `SV_RunPMoveForEntity` declaration | Donor/build compatibility and the existing public PM surface. |

## Generic difference intentionally removed

The pinned VR source and staged port classified any `jumpspeed > 270` as VR in
`PM_IsVRMove()`. That heuristic leaked VR swim and waterjump rules into non-VR
mods, so the QSS generic-baseline rule narrows it to `cmd.vr_active`.

Retained differences are explicit adaptations and correctness fixes, not a claim
of blanket generic parity. The movevar/stat glue and runtime behavior still need
the qualification below.

## Local verification

The focused production-source fixture passes ASan/UBSan with both donor hull
implementations. It covers QSS-style single-step and private explicit-duration
walk/jump, VR ladder pitch independence, raised-jump-speed swim gating, once-only
roomscale and freeze, touch ordering/velocities, safe-origin recovery, and the
existing box/rotated-brush/content checks. Touch-policy checks call the production
helper directly; they do not execute QuakeC impact callbacks. Safe-origin recovery
uses the real donor collision path and nudge fallback. Waterjump launch scaling
is not covered by the swimming test.

## Uncertain / deferred

The narrowed VR predicate is correct for accepted remote VR commands: the pinned
server derives `is_vr_client` from the accepted command's `vr_active`. There is a
local-player exception to resolve before activation: the pinned client sets
`vr_active` for controller aim, but the local server also recognizes VR players
in other aim modes and supplies their VR jump speed. Those commands previously
used the jump-speed heuristic for swimming/waterjump. Supply that movement policy
explicitly at the existing owner boundary; do not set the controller-specific
wire flag merely to enable swimming, since it also controls ladder/roomscale
behavior. This slice does not activate either path.

`PM_SetBaseMoveVars`, moveflag packing, extended movevar stats, and the exact
server-side selection of per-client VR jump speed are retained staged glue, not
claimed QSS parity. They require the later client-versus-pinned-dedicated-server
activation proof. Likewise Gorilla generation admission and authoritative replay
state cannot be established by this unit fixture.

The next gameplay proof remains a built client against the pinned dedicated peer:
receive an admitted PM snapshot/stat baseline, replay acknowledged commands, then
verify reconciliation plus freeze/teleport/discontinuity behavior.
