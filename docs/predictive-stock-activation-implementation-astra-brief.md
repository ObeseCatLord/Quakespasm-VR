# Stock activation: final Astra implementation brief

## Decision and scope

Decide whether this bounded ordinary-stock activation is ready to commit on
`2.0`, not whether the whole migration is complete. Solo operator: no new
framework or ceremonial gates. Plans `63d44ae1` and `0543cfd3` preceded production
edits; the second reopened the lifecycle boundary after your design findings.
Production is frozen pending this review. Read-only; do not edit, run tests,
spawn agents or re-review unrelated renderer/protocol/body/brush code.

## Environment and verified evidence

| Fact | Evidence / limitation |
| --- | --- |
| Authoritative workspace | `quakespasm-2.0`, branch `2.0`; user-dirty `docs/migration-2.0.md` is excluded from every edit/commit. Canonical assets are read-only in `quakespasm_straight`. Disposable test profiles are under `/tmp/qsvr-native-transitions`. |
| Behavioral reference | Existing vkQuake QC/native phase and world owners; existing private queue/PMove/completion/replay for selected stock. Pinned stock QC size340014/crc0bf8/folded-MD4cf69c3e2. No new protocol, transport, state machine or solver. |
| Default and admission | [verified source] `sv_main.c:178,855–929`: selected default1, shared exact-stock identity, initial robust elevators>=3 guard even on static spawn. Existing profile/stat/socket/multi-slot/loadgame/dry-WALK/customphysics/trusted-hand guards remain. Multi-slot listen hosts are not specially excluded. Repeated begin does not reselect; the setting controls initial admission, not live revocation. |
| Living native freeze | [verified source/QC] `sv_phys.c:7477–7518`: exact-stock, live/nonfree owner, finite positive health, DEAD_NO, NONE/NOT, typed/bounded/finite positive `intermission_running`; classify as existing native state before stock hull. Dead/terminal predicate and strict post-callback WALK validation remain separate. Actual installed exit/finale stores NONE/NOT at5858/5860 and20878/20880. |
| Mid-callback handoff | [verified source] `sv_phys.c:7842,8044,8076,8193,8253`: reuse phase-aware native continuation after already-executed PreThink/weapon Think, restore world clocks, retire contacts/timers, commit only through existing completion tail. Current NONE dispatch is reselected after weapon Think only for demonstrated selected stock freeze or existing Gorilla dispatch (`9012`). |
| Late/quiet boundary | [verified source] `sv_phys.c:8133,8521–8603`: PostThink/movement/contact freeze retains once-only completion, native authority, cleared hand/timer state; no second movement pass or candidate hand publication. Existing native loop owns subsequent frozen quiet/batched frames. |
| Completed local checks | [verified command output] Final SDL3 Linux `-Werror` build exit0 (`/tmp/qsvr-stock-activation-linux-final-build.log`). Untouched-default negotiation, mixed generated private-VR/public-desktop movement/fire/replay/mode/death/teleport/pause/gap chain, explicit native0 comparison, wet/ledge/pause and planted/pending pusher chains pass. Seven initial native admission cases pass: wet, noclip, disabled, load, wrong program, single slot, legacy elevators. |
| Actual stock progression | [verified command output/source] New intermission driver passes e1m1 geometry-trigger dispatch, installed due exit Think, reliable parser intermission, quiet/batched frozen native clocks/ACKs/no replay, deadline/button IntermissionThink/NextLevel, actual queued host changelevel to e1m2 and renewed serverinfo/spawn/begin selection. Native0 comparison passes. Finale uses actual end-map boss/train and installed `th_die` callback with prepared caller activation, not natural boss combat. |
| Phase and sanitizer evidence | [verified command output] Final partial ASan/UBSan executable passes nine cases: default exit, native exit, prepared finale, PreThink/Think/PostThink batch and quiet compositions. Each composition invokes real stock exit QC after an actual callback; first freeze asserts one Pre/Post opportunity and only current queue-head completion. Subsequent frame retires pending tail natively. No world integration after freeze. Logs `/tmp/qsvr-stock-intermission-asan-final-*.log`; included physics/server/client owners instrumented, remaining engine objects normal, leak detection disabled. Existing included CL_SetInfo warning exception only for sanitizer compilation; ordinary `-Werror` build unchanged. |
| Component seams | [verified source] Generated input, prepared signon/resources and captured sends are existing boundaries. Actual map reload runs in ordinary dedicated/no-active-QC command context. Only `NET_SendToAll` reconnect broadcast is captured/asserted (one actual reconnect message); no delivery, ACK simulation or alternate transport. Load/program/slot negatives prepare metadata; they are not save/load/mod/local-transport acceptance. GDB probe Python blocks parse, no ptrace execution. |
| Unknown/deferred | [unknown] Fully connected map reconnect and mixed gameplay under all mods; actual device input, eye tracking, Windows/ARM and performance. User explicitly defers those. Arbitrary/AD/q30 command-time admission and cooperative QC integration remain parent implementation stages. Initial wet/load/single-slot cases stay native; do not portray them as complete prediction. |

## Open calls and current lean

1. Accept minimal native frozen-state adapter and selected default activation?
   Lean yes for this exact stock contract. Replacement duplicates QC lifetime,
   native dispatch or completion; blanket NONE admission hides malformed states.
2. Any demonstrated valid-stock receipt/snapshot/callback rejection still blocks
   this default? Verify the actual changed owners and fixture assertions before
   judging. Distinguish source bugs from connected/device qualification limits.
3. Does the handoff accidentally change death/native/public behavior, repeat a
   callback/integration, lose pending completion, publish stale replay or add
   needless policy? Deletion/simplification is in scope; another movement owner
   requires reopening the plan, not an incidental fix.

Return a terminal review <=650 words: effective model/effort and reviewed scope;
prioritized findings with file/line evidence; accept/block disposition and only
meaningful additional checks. No broad rediscovery or new feature wishlist.

## Bounded follow-up after final review P2

Your delayed-contact finding was checked and the eligibility correction was
planned/committed as `a94db1de` before its production edit. [verified source]
`SV_VRContactSampleValid` now shares the same pure frozen predicate through a
static forward declaration, gated by existing selection; queue/drain/cursor/
continuity and movement completion owners are unchanged.

[verified actual-code output] The new `-delayedcontacts` sequence uses actual
stock impulse1, encoded live axe contacts and a real QC whiff/cooldown positive
control. The same encoded samples arrive after actual freeze, before reliable
intermission parsing. Default-selected, native0 and prepared finale pass with
no cue/cooldown/hostility change; invalidated continuity and ordinary frozen
callback/ACK/map progression obligations still hold.

[verified negative control] A disposable `/tmp/qsvr-frozen-contact-negative`
source copy removes only the new eligibility guard. It retains live-validity
and no-effects assertions, bypassing earlier frozen predicate/continuity
assertions only to reach the side effects. It reproduces a frozen axe cue
delta1, cooldown/hostility delta0.775 and aborts at the original no-effects
assertion. Production with the guard passes. No temporary control policy is
added to production or committed fixtures.

[verified] Final Linux `-Werror` rebuild after the contact guard passes
(`/tmp/qsvr-stock-activation-contact-linux-build.log`). Focused updated sanitizer
matrix results are recorded in the activation plan after execution. Follow-up
review scope is only this contact guard and affected integrated driver/evidence;
the prior accepted movement adapter/default decisions need no broad re-review.
Return a terminal follow-up <=350 words with effective model/effort, findings
and accept/block. Read-only, no tests/edits/subagents.
