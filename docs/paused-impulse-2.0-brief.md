# Reconcile deliberate impulses with the existing private pause fence

2026-09-30. MOD-013 investigation/before-code brief. No production change yet;
this is not authorization to weaken the accepted prediction pause/recovery
owner. Source baseline47df36e6; unrelated VR-default patch is independently owned.

## Verified source issue and reference limit

Requested-Astra input audit found a concrete client consumption boundary:
CL_SendCmd finalizes a command through CL_FinishMove before CL_SendPrivateMove.
Finalization copies in_impulse then clears it. With pinned private framing and
cl.paused, the private sender returns before recording/sending that command.
A deliberate impulse entered during the pause can therefore be consumed without
an admitted command. CL_PrivateMoveResumeObserved also intentionally clears
impulse/button edges/pending motion on the new recovery epoch.

Main checked these actual paths in cl_main.c:CL_SendCmd and cl_input.c:
CL_FinishMoveInternal, CL_SendPrivateMove, CL_PrivateMoveResumeObserved. The
retained primary51b452c018273647dcf94f4628a370267ff8fa91 sender lacks the client
paused rejection; the initial audit did not establish its eventual server/QC
impulse result. Native bindlist aliases such as giveall use normal impulse
dispatch when their actual mod definitions exist; do not add a generic engine
giveall command or QSS-M weapon-priority impulse syntax without selected scope.

## Required source investigation before choosing an adapter

Trace the retained and current native/private receiver, pause/resume admission,
command retirement and QC impulse consumption. Distinguish pre-pause sampled
action, deliberate new action while paused, ordinary console-only suspension,
late control metadata, selected versus native private authority, and public
desktop behavior. Establish whether the retained behavior actually preserves a
new impulse until QC runs, and which current owner safely expresses that intent.
The established causal epoch/first-sequence fence must remain authoritative;
no stale duration/attack/tracking replay or client-generated epoch is permitted.

Compare a minimal sampling/acceptance repair using the existing single impulse
latch and native command recorder with an explicit bounded pending-intent tag
only if existing state demonstrably cannot distinguish those cases. Consider
CSQC input filtering and resume clearing before choosing policy. A second
command/impulse queue, alternate transport, broad input rewrite or new recovery
state machine is unsupported. An early return before all input finalization is
not assumed safe; other key sampling, ACK/reliable drainage and QC timing remain.

Proposed source investigation scope: Quake/cl_input.c/cl_main.c/cl_parse.c direct
impulse/pause/sampling boundaries and corresponding sv_user.c/sv_phys.c native/
private command admission/completion/impulse consumption; readonly retained
primary and QSS-M pinned03a498aabc411e2e739adc815c5536b161b9626e. Do not re-audit
all physics/replay/mod admission. Report source-backed choices and smallest
end-to-end vertical, then commit an implementation disposition before coding.
Main retains final architecture; local Astra source advice is required for this
coupled boundary. No runtime/effective-model certification from a source audit.

## Final software qualification boundary

Only after complete implementation, actual console/mod alias input, native
recorder/transport/server-QC paths must cover pause/post-pause impulse intent,
pre-pause cancellation, held buttons, delayed/rapid epoch events, public/private
and local/remote authority, CSQC filtering and lost/resend command delivery.
Preserve existing paused-time discard and newer-epoch permission checks. No
header-only or counter fixture can establish the requested gameplay result.
Builds/tests/compiler/probes/fixtures/game runs remain deferred until all
implementation is finished; Windows and user live trials stay later work.

## Bounded reference audit and design disposition

Requested-Astra source advice established retained preservation beyond the
sender: primary SV_AcceptLatestUsercmd retains the last nonzero impulse across
zero commands (sv_user.c:1388–1394), and the suspended native branch deliberately
keeps impulses while discarding button taps (2453–2462). Main spot-checked both.
The reviewer additionally traced native QC and queued command restoration after
PostThink; QSS-M retains nonzero edict impulses but its independent receipt-time
QC is not a universal pause reference. Current native public/selected bridges
already contain impulse consumers; do not replace the server/QC implementation.

| Source recommendation | Main disposition |
| --- | --- |
| Keep the existing single in_impulse pending value. | Adopt as design constraint: latest explicit assignment wins; no second impulse/command queue or acknowledged duplicate. |
| Add bounded provenance for deliberate input during suspension/recovery. | Adopt in principle, not yet production: identical values from before/after suspension cannot express the required cancellation distinction alone. |
| Project through ordinary pre-CSQC input and consume on eligible recording. | Adopt: filtering must be able to suppress/replace once; never inject after QC or retry a suppressed impulse indefinitely. The exact existing sampling/recording seam must be specified before coding. |
| Use accepted selected-server epoch/positive first-sequence readiness. | Adopt: zero-sequence pause observation grants no release. Do not wait for prediction permission or recovery completion, because the first correctly fenced command must carry new actions. Reserve startup sequences0/1 as before. |
| Console-only suspension cannot be inferred from cl.paused alone. | Confirmed open decision: the existing local single-player server also suspends at key_dest != key_game. Establish an authoritative observation boundary without manufacturing an epoch or stranding a console opening the server never observed. |
| Preserve existing admission/recovery and lifecycle resets. | Adopt: no paused duration, stale attack/tracking replay, alternate transport, or client-created epoch. Specify successive suspension and delayed older-event cancellation before implementation. |

Main rechecked CL_FinishMove/CL_SendCmd pre-CSQC ordering, the private paused
return, reserved-sequence recorder, resume clear and the validated parser's
zero/positive marker distinction. These support the gap and constraints, not a
complete corrected adapter. The console observation and exact one-time
consumption decisions remain unresolved implementation work; this brief is
not a claim that MOD-013 is closed. A narrower mostly-worked design and bounded
source review must resolve them before any production edit. Requested Astra
xhigh source advice has no available effective-runtime settings certification;
no execution or formal final-goal signoff is claimed.

## Subsequent implementation disposition

The [mostly-worked adapter plan](paused-impulse-adapter-2.0-plan.md) resolved
console observation through exact loopback identity and the read-only existing
server phase. Requested-Astra caught an additional already-due arrival-gap case;
the adopted shared predicate preserves the original terminal/timeout owner.
Commitc8d5545a implements that bounded input adapter with main complete-diff
review and final independent requested-Astra source review finding no P1/P2.
This supersedes the earlier open console/projection design questions, not the
final software/QC qualification or broader migration requirements. No execution
checks or additional impulse protocol/queue/recovery owner were introduced.
