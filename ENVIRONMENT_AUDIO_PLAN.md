# Environment audio investigation and implementation plan

Investigated 2026-09-10. This is a design and source/map inspection, not an
implemented or benchmarked acoustics feature.

## Recommendation and agreed scope

Build Linux-first on the existing Steam Audio renderer. Aim for noticeable room
character and muffling while preserving speech intelligibility. Use the installed
base Quake and Quake Brutalist Jam 3 maps as acceptance targets. Radio voice gets
receiver-side bandpass coloration and stays free of game-world occlusion/reverb.
Keep the existing distance-to-radio blend and network protocol.

The user approved this plan and refined the first implementation scope: prioritize
Steam Audio's listener simulation for desktop VR, omit the guessed-room approach,
and defer moving meshes/doors. Sound origins inside geometry may produce audible
artifacts; comprehensive correction is not an initial acceptance requirement.
Add local microphone self-reverb and room sends for local movement sounds so
speaking, walking, and jumping can reveal the room's acoustic character.
Self-reverb has a separate local setting and works in single-player and with PTT
released; PTT continues to control transmission only.

Deliver radio coloration and direct occlusion independently, then implement one
listener-centered realtime room simulation. Start its rendering with parametric
reverb, and compare hybrid rendering before choosing the normal quality setting.
Hybrid is the preferred richer target if profiling and listening justify it.
Defer source-specific reflections, baked probes, acoustic pathing, and GPU work.

The useful distinction is between **where we simulate** and **how we render**.
A listener-centered room response can use parametric, hybrid, or convolution
rendering. Choosing parametric rendering does not eliminate ray tracing if its
parameters still come from the physical simulation.

## Verified integration baseline

- Fetched upstream `origin/master`: `508f1aa0`, “Merge PR #2: add optional Steam
  Audio with review fixes.” The current worktree remains on `55d2b405`; no branch
  switch or engine changes were made. New implementation should start from the
  merged revision, including its playback-progress publication/retirement fixes.
- [Renderer](Quake/snd_steamaudio.c) owns 48 kHz, 256-frame DSP blocks, per-source
  HRTF state, mono voice queues, and a dry music queue. A block is 5.333 ms.
  Positional voice and centered radio already have separate gain contributions.
- [Engine boundary](Quake/snd_spatial.c) publishes source/listener snapshots.
  Remote mouth placement already uses visibility-independent pose interpolation
  when a current entity position exists. Missing positions already use radio,
  or silence in positional-only mode.
- [Voice receive](Quake/voice.c) decodes once and passes mono to this renderer.
  The legacy path currently combines positional/radio gains earlier; a shared
  radio-filter helper could support it later without requiring Steam Audio.
- [Nix dependency](nix/steamaudio.nix) pins Steam Audio 4.8.1. Its built-in CPU ray
  tracer is available; Embree, Radeon Rays, and TrueAudio Next are disabled.
  Realtime acoustics do not intrinsically require a new dependency or SDK update.
- [World trace](Quake/chase.c) uses the client world hull only.
  `CL_TraceLine` in [r_part_fte.c](Quake/r_part_fte.c) also visits client brush
  entities and accounts for translation. Its explicit rotation/start-solid gaps,
  static entity cache, and access to mutable client state prevent using it as an
  unmodified off-thread acoustic tracer.

## Feasible effects

| Effect | What it adds | Main work / limitation |
|---|---|---|
| Radio bandpass | Immediate audible distinction from nearby speech | Small; separate filter history and smooth blend |
| Quake direct occlusion | Quieter, duller sound behind static walls | Small-to-medium; trace policy and smoothing |
| Simulated listener-room parametric reverb | Geometry/material-dependent decay | Medium-to-large; scene export, worker, lifetime management |
| Simulated listener-room hybrid reverb | Adds directional early echoes | Same scene/scheduler, more DSP and IR ownership work |
| Local microphone self-reverb | Hear the room respond to one's own speech | Reuse room simulation; local capture routing and latency matter |
| Source-specific reflections | Source-to-room-to-listener geometry matters | Large; source scheduling, one-shot onset, more simulation/DSP |

Steam Audio explicitly documents placing a simulation source at the listener
to obtain one room response independent of actual emitter positions. A shared
send can then use that response; simulation cost does not scale with the number
of send contributors. This models the listener's space, so a speaker in another
room inherits the listener's reverb character. That is an accepted approximation
to propose for this co-op implementation, not accurate inter-room propagation.
[Steam Audio programmer's guide](https://valvesoftware.github.io/steam-audio/doc/capi/guide.html#reverb)

Parametric rendering uses three-band decay times and an artificial reverb; it
does not reproduce individual outdoor echoes. Hybrid uses convolution for the
early part and a parametric late tail. Full convolution retains more reflection
detail at higher rendering cost. None of these rendering choices requires baked
probes. [Reflection effect API](https://valvesoftware.github.io/steam-audio/doc/capi/reflections-effect.html)

### Radio coloration

Split decoded mono into positional and radio branches before filtering. Apply
a high-pass plus low-pass only to radio, then mix it centered with the existing
radio gain. Start listening around 300 Hz–3.4 kHz; compare a gentler 200 Hz–4 kHz
setting. These are proposed artistic starting points, not measured best settings.
Use non-resonant filters, modest gain compensation, and a bypass for A/B.

Keep persistent per-speaker filter state, advance/clear it deliberately through
silence, and smooth radio gain/enable transitions. Reset on mute, disconnect,
speaker-slot reuse, and receive disable. Normal packet boundaries must not reset
the filter. Preserve VAD, jitter/PLC behavior, received-voice gain, and latency.

There is no need to narrow the transmitted signal: the same decoded packet can
contribute to both branches, and every listener has a different blend. Include
gentle compression and optional mild saturation in radio listening experiments,
after establishing the bandpass baseline. Tune these together with radio gain,
voice distance/blend, and direct positional gain; processing stays receiver-side
and specific to the radio branch. Keep hiss and squelch clicks outside initial
scope. Compressor gain reduction and makeup gain must not create clipping or
pump audibly between talkspurts.

### Direct occlusion

Start with a dedicated main-thread audio trace over world hull 0. Reuse the
existing hull traversal rather than treating camera/particle trace wrappers as a
complete acoustic API. Use static world geometry only in the first increment;
translated and rotated brush entities are optional follow-up work.

For an active source, sample one line initially and optionally a few small
endpoint offsets near transitions. Convert obstruction to both attenuation and
low-pass filtering before HRTF/panning. Steam Audio's scalar occlusion alone is
gain reduction; frequency-dependent transmission is a separate feature. A small
engine-owned low-pass is sufficient for the intended muffling.
[Direct effect API](https://valvesoftware.github.io/steam-audio/doc/capi/direct-effect.html)

Proposed starting envelope: update ongoing sources around 10–20 Hz, with an
initial trace when a new source starts; interpolate coefficients/gain over
roughly 50–150 ms. Give speaking players and newly audible transients priority
under a global ray budget. Experiment with a blocked-path floor near -6 to -12 dB
and a 1–3 kHz low-pass, using a gentler voice profile. Do not turn a single blocked
ray into silence. Multiple rays soften doorframe transitions; they are not a
diffraction solver and do not relocate sound to a doorway.

Preserve source-generation checks and finite/bounded output. Use an inexpensive
fallback when a trace reports an unusable start-solid result, but accept
imperfect muffling from embedded emitters/muzzles instead of implementing source
repositioning, wall-thickness recovery, or special trace suites initially.
The blocked-path floor already limits how much a false obstruction can suppress
the source. Exclude UI/music/radio and tiny player/monster bodies.
Keep distance attenuation in Quake units as today; do not apply a second SDK
distance falloff on top.

### Room estimation decision

A six-direction trace can approximate room dimensions/openness. Steam Audio's
parametric effect accepts decay times directly, so a custom estimator can drive
it without a scene or simulator. There is no automatic C API “cube-cast room
reverb” switch in the inspected API. We would implement the estimator, sky/open
space detection, absorption assumptions, predelay/wet level, and smoothing.

The user prefers Steam Audio's engineered simulation approach if it meets the
desktop VR budget. Do not implement or maintain a guessed-box estimator in this
plan. If scene cost is too high, first profile and reduce simulation frequency,
ray/bounce count, rendering quality, or geometry complexity. A separate room
estimator would be a new design decision, not an automatic parallel workstream.

## Geometry and materials

Export loaded `model_t` world surfaces at map load using `firstmodelsurface` and
`nummodelsurfaces`, `surfedges`, `edges`, and `vertexes`. Triangulate original
convex BSP face polygons; do not copy water's subdivided render polygons or GPU
buffers. Working from normalized engine structures covers BSP29/BSP2 without
adding another runtime BSP parser.

Export the full world, not only the rendered PVS. Restrict the world export to
its face range: the backing arrays also contain inline submodel faces. Including
those indiscriminately would freeze doors in their initial geometry and later
duplicate them when instances are added. Skip degenerate triangles and validate
winding/normals after coordinate conversion.

Steam Audio materials provide three absorption bands, scattering, and direct
transmission. Its scene API supports triangle materials and independently moved
instances. A custom ray tracer is also supported, but would need correct hit
distances, normals/materials, ray intervals, and worker-safe geometry access.
[Scene API](https://valvesoftware.github.io/steam-audio/doc/capi/scene.html)

Use a short, deterministic material table: concrete/brick/rock fallback, plus
recognizable metal/wood/glass names. Strip texture-animation prefixes before
classification. Begin with a uniform moderately absorptive hard-surface preset,
then A/B texture heuristics. Do not make every unknown surface nearly perfectly
reflective. Scattering and absorption give broad acoustic character, not the
resonant sound of a physically modeled metal plate. Optional texture overrides
can be lightweight text rules later, without baked acoustic assets.

Sky should be acoustically open; omitting sky triangles avoids a false ceiling.
Initially omit turbulent liquid/teleporter faces and treat underwater sound as a
separate future policy. Otherwise a renderer's water boundary may become an
unintended concrete wall. Alpha-tested fences/grates and visual-only brush props
also need explicit porous/omit rules instead of pretending every polygon is solid.

Use one documented world-to-SDK coordinate transform and meter scale for all
vertices, sources, listeners, normals, and instance transforms. Existing VR code
uses `vr_world_scale / (1.5 * 0.0254)` units per meter. At the default scale that
suggests 0.0381 meters per Quake unit as a starting acoustic reference. Keep this
reference stable across desktop/VR and independent of personal scale adjustments;
tune deliberately because it controls reflection delays and decay. Validate the
transform with a known room. Current HRTF direction calculations alone do not
validate a physical scene's scale.
[SDK coordinate convention](https://valvesoftware.github.io/steam-audio/doc/capi/geometry.html)

### Installed-map size evidence

A read-only scan of the local BSP face lists counted fan triangles (`edges-2`)
in world-model surfaces, excluding texture names beginning with `sky` or `*`.
These are approximate exporter input sizes, not measured runtime cost, final
scene triangle counts, or a guarantee that the installed base maps are stock.

| Installed map | Estimated world triangles | Inline brush submodels |
|---|---:|---:|
| id1/e1m1 | 15,178 | 57 |
| id1/e1m2 | 13,615 | 61 |
| qbj3_avix2 | 7,901 | 38 |
| qbj3_pinchy | 418,618 | 1,482 |
| qbj3_colossus | 485,004 | 1,065 |
| qbj3_draqu | 567,531 | 385 |
| qbj3_hcm | 794,206 | 2,933 |

The 64 installed `qbj3_*.bsp` maps include 35 BSP29 and 29 BSP2 files. Frequently
used textures include `conc_c01_gry3`, `conc_c01_blk1`, `conc_t02_wht1`, and
`metal_iron3_01`, supporting simple concrete/metal name rules. Names such as
`tch_c1_blk1` remain ambiguous and should use the fallback.

Do not instantiate every submodel as a moving acoustic object: submodel count
is not an active-door count. Profile scene creation time and memory as well as
simulation. Start with the SDK's existing built-in CPU backend. Consider a
package-scoped Embree experiment only if profiling identifies ray tracing as the
problem; it is not currently linked, and its SDK build integration needs checking.
Geometry simplification should preserve openings and material boundaries and be
motivated by measurements.

## Room simulation, mix, and ownership

The main thread exports immutable geometry and publishes latest listener/source
positions and brush transforms. A worker owns the scene/simulator and applies
updates between simulation runs. The audio callback owns DSP state and consumes
completed results without waiting. Never trace against mutable `cl.entities`,
read Quake's movable cache/hunk, build a scene, or allocate in the callback.

Start profiling with one listener-centered simulation source, one worker thread,
roughly 1,024–4,096 rays, 8–16 bounces, 1–2 seconds of simulated decay, and 5–10 Hz
updates. Use order 0 for the initial parametric estimator and compare first-order
Ambisonics for hybrid output, initially around 100–200 ms of early convolution.
These are test ranges, not a promised CPU budget or final defaults. Low bounce
counts can bias hard-room decay; increase quality only after measuring error and
cost. Reuse the latest result when the worker is late; coalesce updates rather
than queuing a growing backlog. Large movement/teleports need special refresh and
wet fade handling. Rotation should use the latest headset orientation in the
Ambisonics decode, with a consistent simulation reference basis.

SDK simulation/scene commits are blocking and have synchronization restrictions.
The worker should retrieve scalar outputs after each completed run and publish
a coherent snapshot. Keep map/source generations and retire scene/source handles
only after both simulation and audio have stopped referencing them.
[Simulation API](https://valvesoftware.github.io/steam-audio/doc/capi/simulation.html)

An important 4.8.1 implementation constraint: `outputs.reflections.ir` points to
the source's internal triple buffer. The convolution effect consumes an update
and swaps its own previous IR into that buffer. Treat it as one producer/one
effect consumer, not as an immutable IR that can be copied into many effects.
This conclusion comes from the pinned SDK implementation:
[output handoff](https://github.com/ValveSoftware/steam-audio/blob/v4.8.1/core/src/core/api_simulator.cpp),
[convolution consumption](https://github.com/ValveSoftware/steam-audio/blob/v4.8.1/core/src/core/overlap_save_convolution_effect.cpp).

Proposed bus arrangement:

```text
SFX mono --------+--> direct gain + muffling --> HRTF/panning -----+
                 +--> SFX room send --> room reverb -------------+
                                                               +--> final mix
Voice mono ------+--> positional gain + muffling --> HRTF --------+
                 +--> positional voice room send --> voice tail -+
                 +--> radio bandpass + radio gain --> centered --+
Local mic ----------> own-voice send --> room reverb only -------+
Music/UI -------------------------------------------------------+
```

First use separate SFX and voice parametric effects driven by copies of the same
room decay parameters. This keeps voice resets independent of SFX tails. For the
hybrid comparison, upgrade the SFX room effect to consume the simulated IR while
voice retains a lighter parametric tail from the same simulation. The inspected
SDK parametric implementation writes an omnidirectional mono tail; it does not
create directional early reflections merely by requesting more channels.
[Parametric implementation](https://github.com/ValveSoftware/steam-audio/blob/v4.8.1/core/src/core/reverb_effect.cpp)

If matching hybrid reflections on voice are audibly worthwhile, separately test
a single combined SFX/voice hybrid bus or independently owned simulation/IR
streams. The combined bus is cheaper, but muting speech must flush its shared
tail, momentarily cutting SFX reverb too. Do not silently introduce unsafe IR
sharing to avoid this tradeoff. A shared voice-only tail can initially be flushed
when any speaker is muted; per-speaker parametric states are a later refinement.

Natural sample completion must stop new sends while allowing existing wet tails
to drain. Source-channel reuse must not erase an already emitted room tail.
Explicit remote mute/receive-disable/disconnect/map reset must clear queued
remote voice and its effects. Local self-reverb has its own enable/lifecycle;
disabling remote reception or releasing PTT must not disable local room feedback.
Acoustic DSP lifetime is therefore separate from the source cursor
and its short HRTF tail. Keep the upstream cursor/retirement fix intact.

Make reverb-send eligibility separate from `SA_DRY` versus `SA_POSITIONAL`.
Local footsteps, landings, jump sounds, and desktop local guns currently use a
dry direct path but should still excite the room; UI/menu sounds should not.
VR muzzle sounds already have useful positions. The installed QBJ3 QuakeC source
in `qbj3/src/player.qc` emits `player/footstep*.wav` and `player/waterstep*.wav`;
`qbj3/src/client.qc` emits `player/player_jump*.wav`. Route existing movement
sounds into the wet send, without adding a new footstep generator for mods that
do not emit them. The haptic exclusions in `CL_IsExcludedLocalHapticSample` are
not audio exclusions and must not be reused to suppress their reverb.
Initially leave leaf ambience/music dry and use lower sends for continuous loops
and voice than for weapon transients. Map/mod sound classification needs a
conservative fallback because Quake does not supply a complete semantic bus map.

Take the room send from mono before direct-path low-pass/HRTF, with its own
distance and obstruction weighting. Do not automatically silence reverb when the
direct ray is blocked, or allow an unrestricted wet signal to make every wall
sound transparent. A shared room send needs artistic coupling limits precisely
because it does not solve source-to-listener propagation. Avoid double distance
attenuation if source-specific simulated reflections are added later.

Begin voice sends lower and cap/scaledown long high-frequency decay; preserve a
clear dry onset. Control wet output/EQ and clipping with dense gunfire plus
multiple voices. Retain current received-voice level and avoid relying on the
final hard clipper for headroom. Listen at matched perceived loudness so “better”
does not only mean louder. Do not promise a fixed universal wet percentage before
listening to these authored samples.

### Local microphone self-reverb

Use the already selected microphone's mono PCM before Opus encoding/network
packetization. Fork it into a bounded local effects input and the existing dry
voice-chat pipeline. The local branch produces wet output only: no dry mic
monitor, radio branch, direct HRTF copy, self-occlusion, received-voice boost, or
network jitter buffer. The player's naturally heard voice supplies the direct
sound. Never feed rendered room effects back into the encoder.

Use an independent own-voice wet level and enable setting, separate from received
voice volume/reverb and transmit permission. A single microphone device can serve
both purposes. The capture state should depend on whether local self-reverb or
authorized voice transmission needs it; network encoding/packet generation still
obeys the existing session, permission, and VAD/PTT gates. Self-reverb works during
active single-player or multiplayer gameplay even when PTT is released or network
transmission is disabled. Disable/suspend its feed outside an active game room or
on device/runtime loss; reset its queued PCM/tail on local disable and map change.
PTT release can end transmission while the local room response continues.

The current `Voice_OpenCapture` requires transmit to be enabled and
`Voice_StopTransmit` also closes capture, so these responsibilities need splitting.
Capture consent, device availability, local monitoring, and network transmission
cannot remain one boolean. Save the new local setting through the existing
physical-menu preference mechanism; its version-1 fixed-size settings format
needs a backward-compatible migration retaining device, mode, transmit, and PTT
choices. Show local microphone use separately from “transmitting” so single-player
self-reverb does not masquerade as an active voice-chat session. Start the new
local effect disabled until selected; ordinary server/config commands must not
enable a new microphone use.

For the first parametric room prototype, self audio can feed the SFX room effect
with its own send gain. The hybrid comparison must include self early reflections,
not only a parametric own-voice tail. Sharing the SFX hybrid effect preserves one
consumer of the simulated IR. On self-reverb disable, flush that shared wet state
if necessary; a brief loss of SFX tails is an acceptable first-pass tradeoff.
Separating self hybrid state later requires independent IR ownership, as described
above. The listener-centered source approximation is particularly appropriate
for one's own mouth; defer exact mouth offset/directivity.

Latency is an acceptance concern for this effect. The current queued SDL capture
request and `Voice_Frame` dequeue/processing use 960 samples at 48 kHz (20 ms),
followed by a main-thread pump and output-device buffering. Do not add network
preroll or playout delay to local monitoring, and do not describe the 5.333 ms DSP
block as end-to-end latency. Measure input-to-first-wet-output delay and audition
short-room reflections. If the existing path sounds like a delayed voice repeat,
use a smaller local capture chunk/ring while separately accumulating the 960
samples required for Opus/VAD. A dedicated capture callback may help if the
main-thread pump dominates; this remains a measured follow-up rather than an
unconditional capture rewrite. Hardware capture/output buffering still applies.

Use a gentle local noise gate/expander if mic noise continually excites the room,
with smooth release and no repeated network preroll. Let tails decay after the
input gate closes. Keep the local effect subtle enough to preserve natural speech;
check headphone leakage for acoustic recirculation in actual headset listening.
These checks concern the audible local feedback path, not a change to network VAD.

## Moving doors and networking

No new audio packets are required. Both occlusion and scene-instance updates can
use brush transforms already received by the client. Local state may be absent
or stale outside network visibility; the client clears models missing from the
latest update in `CL_RelinkEntities`. Accept leakage and drop/fade invalid dynamic
occluders instead of retaining a permanently closed door. Correctly synchronized
remote acoustics would require more authoritative state, which is outside this
co-op scope.

First occlusion and room-reverb iterations omit moving brush geometry. Only if
listening motivates follow-up work, add selected
large door/lift instances from client snapshots, including rotations through the
same transform used for rendering. Add/remove/move instances and commit only
between worker simulation passes. Neither the game thread nor audio callback
should wait for a moving door's reflection solution.

## Delivery sequence and acceptance gates

1. **Radio coloration — small independent increment.** Add receiver-side filter,
   state/reset handling, smooth blend and bypass. Verify nearby positional audio
   is unchanged, distant/unknown-position radio is colored, radio remains dry,
   and positional-only mode has no radio output. Listen to actual two-client
   relay at normal gain, including transitions and packet-loss recovery.
2. **Direct muffling — small-to-medium increment.** Add bounded main-thread world
   tracing, source generations, obstruction snapshots, and smoothed gain/low-pass.
   Test same room, a corner, thin wall, and static doorway. Embedded emitters and
   local muzzles must remain numerically safe but need not be acoustically exact.
   Moving doors and rotations are deferred. Preserve dry/HRTF responsiveness.
3. **Acoustic scene and room-reverb experiment — largest first milestone.** Export
   static geometry, create worker/result lifecycle, then render parametric room
   reverb with independently controlled voice/SFX sends, including existing local
   movement sounds. Measure base-map and
   large-QBJ3 scene load/memory, worker timing, result age, and callback cost.
   This decides whether the physical scene is a practical default.
4. **Self-reverb and hybrid comparison.** Add local wet-only microphone routing,
   its separate single-player/PTT-independent setting, and lifecycle handling.
   Compare the same scene and
   positions with parametric, short hybrid, and bounded full-convolution reference.
   Include self speech and footsteps/jumping as repeatable environment probes.
   Measure capture-to-wet latency, listen for useful early echoes versus smeared
   speech/gunfire, profile under VR load, and choose quality/defaults from results.
5. **Optional follow-ups:** selected moving-door/lift instances, then—only if the
   shared room limitation is objectionable—a small priority pool
   of source-specific reflections for active positional speakers, sustained
   emitters, and selected transients. Retain a shared-room fallback; do not
   allocate/simulate per potential Quake channel. Solve first-shot readiness,
   pool reuse, crossfades, and independent wet-tail lifetime before expanding.

Expose simple user controls for environment amount/quality, received-voice reverb
amount, own-voice room effect/level, and radio coloration; keep ray counts/material
tuning in diagnostics. Provide
independent dry/occlusion/reverb A/B switches during development. Proposed names
are not an API commitment. Environment failure should preserve the existing
direct spatial renderer; `-sndlegacy` remains the full backend fallback.

Extend the focused spatial fixture with meaningful tests for filter frequency
response, arbitrary block partitions, source/mute resets, wet-tail draining,
closed/open/sky test rooms and generation mismatches. Add moving-instance tests
when that feature is implemented. Self-reverb tests must cover wet-only routing,
single-player and PTT-released operation, dry network encoding, capture ownership
when either feature is disabled, settings migration, and bounded local backlog.
Exercise scene teardown while a worker is active and concurrent result handoff
under sanitizers. Keep zero callback allocation/nonfinite output checks. Check
both `USE_STEAMAUDIO=0` and `USE_VOICE=0` build boundaries after code changes.

For performance, report callback percentiles/maxima against 5.333 ms separately
from scene build time, simulation duration/cadence, acoustic result age, and
memory. Test crowded audio and worst-size geometry separately and together.
No existing HRTF-only benchmark establishes the budget for these new effects.

Listening matrix: installed `e1m1`/`e1m2`, one small QBJ3 map, an open courtyard
and concrete interior, and `qbj3_hcm`/`qbj3_draqu` as geometry stress cases.
Use gunshots, explosions, sustained loops, one and several speaking players,
local mic speech with PTT released and while transmitting, footsteps, landings,
jumps, turning/leaning in VR, doorway crossings, radio transitions, map changes,
and mute/reconnect. Compare a small room, large hall, and open courtyard by
speaking/moving; listen for unwanted delayed self-voice, ambient mic wash, and
feedback buildup. Stage isolated profiles containing QBJ3 assets; the current
spatial launch helper stages id1 only. Preserve the normal Downloads configs.

## What remains unmeasured

No acoustic scene has been created, no new filter has been auditioned, and no
reflection simulation or new DSP benchmark was run in this investigation.
The feasibility claim is based on the actual integration boundary, pinned SDK
API/source, upstream merge review, and installed-map geometry inventory.
Final material values, scene performance on QBJ3, and preferred reverb strength
require the staged implementation/listening work above. The user has supplied
the platform, sound character, radio routing, and test-map preferences needed to
begin that work; there are no outstanding blocking design questions.
