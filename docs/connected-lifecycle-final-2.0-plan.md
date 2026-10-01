# F01 connected map and slot lifecycle: before-code plan

2026-10-01. Extend the existing connected runner and desktop/VR GDB probes,
preserving their existing admission/movement/fire/prediction assertions. Reuse
native UDP, server stdin commands, client commands, scoreboard/parser, QC and
renderer. No transport shim, synthetic negotiation/commands/body/ammo, new
production protocol or runtime owner. Software hand/key input retains the
existing probe boundary; HMD remains runtime-owned.

Current evidence: private and unchanged-public desktop/private VR gameplay passes
but quits before map changes and slot reuse. Native dedicated Sys_ConsoleInput
accepts stdin; CL_ClearState and disconnect already own command/snapshot reset.
Missing evidence does not justify replacing these systems. Initial mixed-run
distance inconsistency remains separate and must not be silently declared fixed.

## Finite vertical proof

1. Existing probes complete gameplay, then optional lifecycle hooks keep both
   clients alive. Record their map, owner/slot, peer names/colors, own and peer
   authoritative positions and private ACK/snapshot state.
2. Runner sends native changelevel e1m2 through its owned server's stdin. Both
   clients complete native signon and render the new map with both identities.
   Observe actual CL_ClearState completion clearing prior commands/snapshot;
   then require fresh advancing movement ACK and coherent private owner.
3. Desktop executes native disconnect. VR observes the retired peer's name
   cleared. Runner launches a fresh replacement desktop profile against the
   same two-slot server. Require Replacement in exactly the retired slot, no
   old identity, and valid private VR owner/prediction state.
4. Use existing neutral-rearmed XR injection and native commands for new
   movement/fire. Observe fresh ACKs, ammunition effects and mutual authoritative
   entity positions after settling; no assigned simulation state.
5. Native quit for owned clients/server, structured positive results/markers,
   normal exits and clean Vulkan validation. Preserve failed logs. Hardware,
   performance, voice and all metadata/packet-loss variants remain separate.

Reuse the runner's process/profile lifecycle and probes' existing controller
injector. One optional shared GDB helper for the three roles, bounded phase
files for orchestration only; no game bridge or production instrumentation.
Before-code ownership: main runner/docs/integration; Luna only shared probe
helper and optional hooks in the two existing GDB files. Main reviews complete
diff and actual results. Stop/replan if this grows into a testing framework or
needs production architecture changes. Main/reference/user migration doc stay
untouched. Existing private Monado service only; no external server deployment.

## Bounded implementation adjustment

Main reviewed the first helper before execution. The delegated300-line estimate
was exceeded (392 lines); reopen the size decision rather than conceal it. The
extra work is GDB exit/resource-reset observation and the three test roles,
including unchanged public layout. It introduces no production state or protocol
and still reuses the original100-line controller injector and complete gameplay
probes. Accept up to425 helper lines and the115-line runner extension for this
finite scenario; no additional test scenario/framework is authorized here.
Main identified ordered-slot versus sorted-name assertions, private-field reads
on public layouts, stale wire observations and synchronous debugger quit as
fixture defects. Correct those at existing test boundaries before execution,
retaining all gameplay assertions. Actual product behavior remains unproven
until the complete native transport/rendering run succeeds.

## First native result and cadence adjustment

Both original probes pass; map signon/reset and peer retirement pass. The newly
launched replacement reaches two peers/private selected prediction,111successful
replay returns, ACK59→226 and shells25→21, but has zero unchanged sent/ACK pairs.
This is the existing observer's cadence limit, not evidence of broken replay.
Before another run, set native host_phys_max_ticrate30 only for the replacement
diagnostic. Retain every gameplay/presentation/jump assertion. The source's
50ms jump timer permits33ms command sampling (the old10Hz desktop diagnostic
did not); this is not default-cadence or performance evidence. No production
changes, guessed poses, synthetic ACK or replay-state injection are justified.

The30Hz replacement run passes all gameplay/map/slot/exercise checks and mutual
pose comparison (0.240/3.884 units), then the helper fails its shutdown assertion:
its Host_Frame stepping breakpoint stops before the queued quit is executed.
Before retrying, disable only that stepping breakpoint for native quit, retaining
fatal error stops and requiring an actual exited event with code0. A forced
debugger cleanup event may omit exit_code; treat it as unknown, never as success.
This is a fixture correction, not a product shutdown repair.

The next run completes gameplay/slot checks but mutual-pose comparison fails
26.791units. Its two recorded samples were taken at different times: Replacement
observed VR still moving before VR's longer neutral-arm/action phase finished.
Keep the16-unit threshold. Add one shared observe phase after both producers
report exercised, then allow native UDP settling and record fresh received poses
on both clients before comparison/quit. No frozen simulation or pose assignment.

The shared-observation run reaches renewed gameplay but VR shells rise25→27
while the owner moves through e1m2. Pickup masking is a plausible explanation,
not proven by the final counter alone. Before retrying, split the existing input
exercise into stationary fire then moving fire. Require an actual received ammo
decrease during stationary fire and fresh ACKs, then retain the moving-wire and
authoritative movement checks. Extend the existing controller injector only
with an explicit lifecycle-only primary-button suppression; retain trigger input
and all original probe behavior. No map-specific conditions, assigned ammo or
removed consumption assertions. This is a measurement boundary correction.

Stationary firing passes (VR25→minimum23; replacement21→minimum19), moving
fire/ACKs and shared received poses pass (0/0units), but VR quit stops again at
Host_Frame breakpoint-1. The helper only disables its own frame breakpoint;
original Python observers are internal and `delete breakpoints` may leave them
alive. Before retrying, explicitly delete the original Python breakpoints via
the GDB object API at the optional lifecycle handoff, then install only the
lifecycle observers. Record unexpected quit stops/exit code. Retain all fatal
stops and actual normal client/server exit requirements. No product quit repair.

The private handoff repeat now passes through natural client/server exits0.
The first unchanged-public lifecycle trial fails the original settled_movement
gate before lifecycle (public ACK69→283, shells25→21; XR probe passes). Reuse
the prior public diagnostic approach: retain already-observed authoritative
origin/owner/displayed vectors in structured samples instead of filtering them
out. No input/timing/assertion changes; a repeat may establish lifecycle behavior
but cannot establish that the intermittent first movement outcome is repaired.

The public diagnostic trial finishes the XR original probe before the public
peer's name becomes available; lifecycle initial_ready fails immediately on
'peer absent'. Before retrying, use the existing bounded pump/until to await
actual signon4 and the two expected nonempty scoreboard identities before
sampling the initial lifecycle baseline. No synthesized peer/readiness flag or
relaxed identity assertion; preserve deadline/fatal stops. Initial XR gameplay
probe alone does not certify simultaneous admission.

Final native outcomes: private handoff-repeat and unchanged-public ready
profiles pass all original/lifecycle statuses/markers, mutualposes0/0units,
all nativeclient/server exits0 and clean Vulkan validation logs. No production
repair needed under this finite transition owner. Both earlier intermittent
original movement failures remain unresolved; passing repeats do not prove
those first failures fixed. Loss/IPv6/metadata boundaries remain separate.

## Focused Astra disposition before final acceptance

Main verified four effective gpt-6-astra/xhigh contexts and spot-checked the
load-bearing source findings. This is a bounded helper/runner review, not F10
overall signoff.

| Recommendation | Disposition |
| --- | --- |
| Post-replacement VR prediction permission/authority/coherent snapshot not reasserted | Adopted before rerun: call the original prediction_sample/require_selected_prediction_state at replacement and final observation, retaining structured evidence. Reuse these owners, no new prediction probe or product instrumentation. |
| Runner success alone omits validation-log acceptance | Adapted: main independently scans all four native logs for each accepted run; document that aggregate status alone does not certify Vulkan validation. No general log framework added. |
| Explicit internal-breakpoint retirement and exact-name admission wait preserve native acceptance | Accepted, main verified source and actual normal-exit private/public results. |

Rerun the two affected private/public lifecycle profiles after adding the
missing post-replacement prediction assertions; earlier passes remain valid
for their narrower boundary but do not establish these new assertions.

Affected reruns: private-selected passes complete native acceptance including
new XR state checkpoints and normal exits. Public-selected passes gameplay and
both XR checkpoints, then unchanged baseline Desktop reports corrupted
double-linked list/SIGABRT on quit. Keep aggregate FAILED and old public-ready
clean-shutdown proof separate; no root cause or repaired baseline claimed.
All four logs in each run have no VUID/synchronization hazards. No new product
source change or reference modification follows from this external-baseline
failure. Precise results/limitations are retained in the current receipt.
