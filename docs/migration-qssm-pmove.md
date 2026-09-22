# QSS-M PMove baseline migration

## Reference and scope

Generic player movement in this slice is based on QSS-M commit
`03a498aabc411e2e739adc815c5536b161b9626e`. The VR wire/command reference is
quakespasm-openvr commit `1327f795cc2e3a8e4f7c9d68e31d64383930cc00`.

The initial solver audit at `6aafc918` covered staged PM code. The subsequent
client linkage described below puts the shared solver and client adapters in the
game binary. Client replay and private protocol admission are still pending.

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
| `PMCL_AddEntities` | Existing client branch of the staged collector, using snapshot poses and the shared physent array. The unused server branch and PMSV/PF wrappers were removed during client linkage; exact staging remains at `6aafc918` for the later real server integration. |
| Header include guard, content-mask guards, `trace_t` spelling | Donor/build compatibility for the shared PM surface. |

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

Server moveflag production, complete authoritative stat receipt and the exact
server-side selection of per-client VR jump speed remain integration work. They
require the later client-versus-pinned-dedicated-server activation proof. Likewise
Gorilla generation admission and authoritative replay state cannot be established
by this unit fixture.

The next gameplay proof remains a built client against the pinned dedicated peer:
receive an admitted PM snapshot/stat baseline, replay acknowledged commands, then
verify reconciliation plus freeze/teleport/discontinuity behavior.

## Production client linkage

`pmove.c` now appears in the Meson, common Make and Visual Studio source lists.
The Linux SDL3/debugoptimized binary links the shared solver, client snapshot
collector and weapon query without unresolved server-policy dependencies.
The disconnected server-only collector branch, PMSV/QC wrappers, local server
cvar registry and base-variable helper were deleted, not replaced with dummy
implementations. Their code is preserved at `6aafc918`; eventual server movement
still needs the real world-area-tree, QC, teleport and Gorilla owners.

Client fallback values now come from QSS-M `03a498aa`'s serverinfo keys/defaults
(`pr_ext.c:2113` onward), rather than local server settings or tracking state.
This deliberately changes the former staging defaults: slidefix and bunny
friction default to zero without server advertisement, jump speed defaults to
270, and missing unstarred `pm_edgefriction` enables QSS edge-box behavior.
The starred watersink/fly/edge-friction keys retain QSS-M's existing spelling;
actual peer publication of those fallback keys is not established here. Boolean
keys use nonzero semantics, matching the QSS server: a fractional value such as
`pm_slidefix=0.5` is true. This intentionally differs from the QSS client
implicit float-to-integer assignment, which would truncate that value to zero.

The donor's full/incremental serverinfo callbacks refresh the PM cache.
`CL_FreeState` invalidates it. Protocol flags are read at selection time, so
serverinfo arriving before protocol setup cannot freeze old precision flags.
Full serverinfo replacement uses bounded string copying instead of reading a
whole destination-sized block from a potentially short command argument. Command
argument storage now matches the existing tokenizer limit; incomplete serverinfo
commands are ignored. The network `svc_stufftext` owner rejects truncated strings
before command dispatch instead of executing a potentially valid-looking prefix.
This retains the existing 2,047-byte network-string capacity; it does not add
support for larger stuffed commands.

Public PREDINFO consumes only shared stats and retains serverinfo-derived extra
settings. Only the explicitly admitted private version consumes private extended
stats and packed flags. Malformed/nonfinite fallback numbers use the QSS default;
unrepresentable integer values are checked before conversion. Invalid used stats
or unsupported private layouts make `PMCL_SetMoveVars` return false. Its true
result proves neither receipt of every stat nor ACK/owner coherence: replay must
establish those separately and honor a false result.

The parameter fixture runs actual client callbacks, Info readers and PM selection
with the real command tokenizer, including long arguments and overflow. Callback
dispatch is explicit. Cache reset is tested directly; full engine resource teardown,
network command dispatch and gameplay are outside that fixture. Windows/ARM remain unverified despite updated build source lists.
