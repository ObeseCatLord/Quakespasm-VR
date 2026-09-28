# Stock moving-brush adapter: implementation review brief

Bounded final source review of the planned slice, not the full migration or
default activation. Read the [committed plan](predictive-stock-pushers-2.0-plan.md)
(`8cc3c95f`, amendments `30c035e5` and `8f361b98`) and actual current sources. Main implemented
after the single authorized web worker failed before edits; no replacement
coding model. Main owns checks/integration while production stays frozen.

| Claim | Evidence/status |
| --- | --- |
| Minimal delta preserves existing architecture. | [verified source] `sv_phys.c` palm contact loop accepts live BSP PUSH contacts in robust mode, marks the existing interaction flag; out-of-world/freed/unsupported legacy cases retain rejection. No solver, callback, carry/rollback, queue, completion, timer or protocol replacement. |
| Quiet palm-only exclusion uses existing accepted state. | [verified source] Pure `sv_main.c:SV_PrivateMoveHasPusherPalm` checks initialized/touching/live BSP PUSH/model-matched anchors at permission production. No new persistent state or solver advance; float model comparison avoids new unsafe float-to-integer conversion. |
| Original disconnect actually occurs. | [verified actual-code run] After preparing the client registration/reliable buffer omitted by dedicated initialization, actual server offer/client handler/capability exchange/raw codec produces the logged unsupported-pusher disconnect before the patch. Prepared headless disconnect resources then fault; do not claim a clean negative transport lifecycle from that run. |
| Existing body support qualifies against native. | [verified actual-code normal runs] Independently initialized selected/native e1m1 lift*7 both rise152 over39 moving frames at25ms, with quiet/batch carry and jump/replay return. Selected10/100ms and generatedVR pass. Carry records equal actual brush displacement; body differences allow only explicit1/8 nudge plus DIST_EPSILON. |
| Foot-plus-palm contact shares one carry, including a stroke. | [verified normal run] Real offer/raw serialization and lift trigger,19 bound moving frames; one subthreshold horizontal hand stroke explicitly removed from the carry oracle. Quiet and two-command world-frame batch included. Local anchor height follows current brush and body; OFF clears state. |
| Palm-only quiet exclusion is exercised without body interaction. | [verified normal10/25/100ms runs] Real static floor beside lift; retained palm binding on stationary and moving brush. Quiet frames assert frame flag0, unchanged completion, RUNNING/unpaused, zero waterjump/no external hold, and full-snapshot replay false. Multiple received commands before one world frame, deliberate stroke and OFF/replay return pass. Another actual public player activates the lift. Physical samples/generated starts remain explicit seams. |
| Blocked movement retains native QC effects and anchors. | [verified normal selected/native plus selected-palms runs] Prepared physical ceiling with actual bbox size; actual stock plat_crush applies1damage, reverses velocity, restores brush/body origins. Wall removal resumes motion. Foot-plus-palm anchor heights remain consistent after rollback. No assertion that callback effects roll back. |
| Callback relocation cannot republish stale hand output. | [verified normal run] Borrowed liquid-contract seam: prepared trigger/destination, actual pinned teleport_touch during selected movement. Epoch/reset generation advance, server hand state clears, complete snapshot has no stale raw baseline/replay permission. |
| Non-rider door push uses the existing world branch. | [verified normal selected/native runs] Actual e1m1 targeted brush*17, prepared invocation of actual installed door_go_up/down functions (no velocity/Think field writes), real BSP contact. Body displacement follows brush direction/budget; support carry record remains0, full snapshot denies selected replay, relocation/ordinary command restores replay. This is not natural map button progression. |
| Surface replacement/retirement owners exist. | [verified source and inspected exit0 normal runs] `PF_setmodel` calls existing `SV_GorillaInvalidateSurface` before model identity changes; `ED_Free` does so before clearing the edict. Focused variants execute the actual registered QC setmodel builtin or ED_Free with palm-only body state, then full snapshot/actual replay. Builtin invocation is a prepared registry/OFS_PARM/argc boundary, not bytecode map reachability. |
| Fresh contact is fenced at publication. | [verified source and before/after actual-code runs] Review identified a pending PMove candidate absent from current client bindings. `-pending-replace` reproduced initialized1/touching3/raw-baseline1 after actual PostThink and prepared actual setmodel invocation. Plan amendment was committed before correction. `SV_PrivateWalkTrialGorillaSurfacesValid` now checks live solid brush/model identity at the existing publication tail, using existing invalidation/cutoff on failure. Both pending replacement/retirement pass with initialized0/touching0/raw-baseline0, then quiet full snapshot/actual replay returns. Movement, callback effects and completion remain committed; the current frame's pusher mark still withholds replay until the next frame. No surface generation owner is introduced. |

Environment: workspace `quakespasm-2.0`, branch `2.0`; user-dirty
`docs/migration-2.0.md` outside writes. Installed pinned QC size340014,
CRC0bf8/hashcf69c3e2 and stock BSPs supplied through read-only pak symlink in
disposable profiles. Actual engine/QC/world/snapshot/parser/replay; captured
socket send and skin upload, prepared client resources/registration/input/start
and selected longer world clocks are explicit component seams. No new transport.
Exact final Linux SDL3 build passes with ordinary `-Werror`.
Seven focused pre-guard combined ASan/UBSan cases passed (body, planted palms,
palm-only, blocked/teleport, door, published replacement/retirement). These
instrument fixture and selected movement/message/client owners, not the whole
engine. The wet/ledge/pause and mixed selected/public arrival-gap regression
drivers passed. Six final partial-instrumentation ASan/UBSan cases pass:
fresh replacement/retirement, planted palms, blocked/teleport and published
replacement/retirement. These include the final publication guard; earlier
unchanged body/palm-only/door paths retain their preceding seven-case evidence.
Device/live
testing, performance and Windows/ARM remain deferred. Selected default off;
AD/load/local-SP/full-domain activation remain parent stages.

Decision: retain the narrow adapter if these contracts suffice. Challenge source
lifetimes and fixture claims, particularly quiet permission, incidental carry,
source/result invalidation and any redundant state/policy. No new simulator,
passive palm carriage, protocol or blanket native fallback is proposed.

Read-only scope: the two production diffs, named shared anchor/carry/rollback/
invalidation boundaries, and `tests/stock_pusher_native_fixture.c`. Do not edit,
run tests/spawn agents, re-review graphics or export operational telemetry.
Verify effective local Astra/max only latest turn_context model/effort fields.
Return <=650words: scope_done/model-effort; ranked source blockers or why
addressed, file/line evidence, minimum remaining qualification and assumptions.
