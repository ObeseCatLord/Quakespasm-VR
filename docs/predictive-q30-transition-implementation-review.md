# q30 classification and dispatch checkpoint review

Scope: the first two component stages of the
[committed transition plan](predictive-q30-transitions-2.0-plan.md), not complete
q30 admission. Normal selection and prediction permission remain closed.

Current implementation uses the existing state enum for typed q30 startup,
camera, hold, ability, hull/mode and gravity-seed qualification. Pre-begin and
spawned checks share the body predicate without changing the client lifetime.
Definition types/bounds, entity offsets and float-to-int domains are explicit;
VM-owned maps remain the lookup owner, with no new cache or persistent state.

At the existing physics batch boundary, current water or a command's native
auxiliary sweep can select fresh native at offset0; a later unstarted head is
deferred to the next frame. The dispatch-only probe restores command, duration,
body/water and exact collision-list position. It runs outside weapon scopes,
retains conservative push-grid entries and does not write pusher records.
Native input/QC keeps its previous water observation. After movement/contact,
qualification loss remains sticky through PostThink, clears credit, and stops
batching without another interval. The stock timer adapter skips q30 ownership.
Native hold/camera eligibility cancels deferred Gorilla input and rejects
physical contact continuity through the existing advancing cursor.

Verified source boundaries: `SV_LinkEdict` appends to its area list; collision
selection retains first equal-fraction hit. The probe therefore restores its
saved next link with the existing primitive. The auxiliary sweep changes public
body/support and links, but not persistent support records. Native WALK refreshes
water after PreThink, so speculative water is restored before native input.
These decisions are from the prior local Astra design review and main spot-check.

Current acceptance work extends the existing exact-q30 fixture for actual native
startup/ordinary return, typed rejection and ability predicates, real QC hold
plus deferred-input cancellation, advancing invalid contacts and probe restore.
The existing ordinary QC/server/client comparisons and stock/public mixed
checks remain relevant. The consolidated results are recorded below.

Open: living transitions first created by PreThink/scheduled Think before
movement, complete ordinary callback closure, real map-trigger traversal and
normal offer/spawn/begin/full-parser/replay. No generic living fallback is added;
the existing whole-world terminal continuation is not proof of those states.
These remain prerequisites to admission and are not retired by this checkpoint.

Requested review: one read-only local Astra Max implementation pass, <=1,000
words with prioritized concrete bugs/risks and file/line evidence. Audit this
bounded production diff (server.h, sv_phys.c, sv_user.c), especially typed state,
probe restoration/duration/list order, later-head deferral, sticky completion,
deferred input and desktop/native regressions. Verify first, then critique.
Do not re-review transport/consumer or treat intentionally closed admission as
a stage defect. Do not edit, test, change branches, commit or spawn more agents.
Missing behavioral evidence should be labeled and retained, not broadened into
a new architecture. Main handles tests, final disposition and integration.

## Implementation review disposition

Main spot-checked the reviewer's source findings and the final changes. Effective
local model/effort was verified as Astra Max; final-turn verification and software
results are recorded below. The review was read-only.

| Concrete finding | Disposition |
| --- | --- |
| Invalidating accepted Gorilla input at a deferred head skips unstarted samples. | **Adopted.** Reset anchors through completed work, retaining pending raw input and the current queue/completion. Actual native consumption is checked. |
| Cleared credit can delay native handoff several ticks. | **Adapted by deletion.** Accepted-head geometry chooses native before command credit. Existing native world-duration/coalesced behavior needs no new scheduler, pending flag or credit restoration. The reviewer independently accepted this boundary; ordinary dry maintenance still replaces uncompleted input with completed levels. |
| Probe can dereference malformed ground before the existing validator. | **Adopted.** Run observational frame validation before probing. Keep structural ground ownership in its existing validator rather than add another reference policy. |
| Sticky qualification loss can mask a newly invalid PostThink body. | **Adopted.** Keep the batching fence sticky and independently validate the current q30 frame before/after PostThink, including maintenance. |
| A reset to completed work can rewind an existing relocation cutoff. | **Adopted.** Preserve max(valid existing cutoff, completed cutoff) while dropping anchors at defer and q30 post-native completion. Actual paired native samples verify normal pending consumption and deliberately invalidated skipping. Stock completion retains its existing reset behavior. |
| Zeroed invalid contact does not isolate hold rejection. | **Adopted.** Build a sample that qualifies with no hold, then show the same sample is refused under the hold and the real contact cursor still advances. |

The first final-pass findings changed production behavior and added targeted
checks. Native callback closure, actual horizontal water entry and normal
admission remain open; the bounded checks do not erase those requirements.

## Final review and software evidence

The final read-only local Astra pass reported no blocking findings in this
bounded correction. Main verified effective `gpt-6-astra` / `max` metadata for
the final turns without exporting session telemetry. Astra checked the shared
cutoff reset, both q30 call sites, native cursor consumption and the paired
fixture; main ran the software checks.

Consolidated Linux SDL3 production build and exact-q30 native fixture exit0.
All six new state/dispatch markers and seven existing ordinary replay cases
pass. The contact check first accepts the actual sample without a hold, then
rejects it under a hold while advancing the real contact cursor. The paired
native-frame check consumes a pending sample and skips one already invalidated
by relocation. Pre-begin state and ability fields are staged; they are not
normal admission or real map-trigger traversal.

Stock and q30 negotiation fixtures exit0, including actual stats/writer/full
parser checks and continued refusal to predict the injected q30 owner. Stock
mixed-session checks pass startup pause, arrival gaps, velocity publication and
native-mode return. The shared real-BSP liquid finder matches its original
implementation verbatim; the stock water/VR fixture passes at 25 ms.

**Unresolved stock check:** the same water/VR run at 10 ms hits the existing ledge
zero-preview assertion at frame 87. Rebuilding with pre-checkpoint `sv_phys.c`
and `sv_user.c` reproduces that assertion. This is evidence that the failure
predates this production diff, not evidence that the 10 ms path is correct. Keep
it open under the stock-liquid contract; do not loosen the assertion or claim
the complete stock liquid matrix passes.

No live headset, eye-tracking, performance measurement, Windows or ARM result is
claimed. Full callback closure, actual horizontal entry, normal q30 admission
and its end-to-end prediction session remain required.
