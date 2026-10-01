# F08 controlled PCM and native voice relay results

2026-10-01. Current preliminary Linux DEBUG Meson engine through production
aa828629; SDL3, USE_VOICECHAT and actual Opus enabled, Steam Audio disabled.
Strict compiler/link flags retained without warning suppression. New313-line
fixture reuses existing mixed/negotiation bootstrap and includes native voice.c;
no production edits or replacement voice/transport/codec owner. Main verified
two effective Luna gpt-6-luna/xhigh contexts and reviewed/integrated all output.

The revised reviewed native run exits0 with VOICE_PCM_NATIVE_PASSED after
Astra's lifetime/acceptance corrections. Earlier final-drop and reproducible
runs exited0 but are superseded: native disconnect could write through prepared
entities freed too early, and mute/reset preconditions were insufficient. The42-line voice-specific builder reads the primary
Meson compile_commands/ninja link command, replaces the five fixture-included
owners and uses the existing four transport/skin wrappers. Library order
preserves Opus under the existing as-needed linker policy.

| Boundary | Actual observed acceptance |
| --- | --- |
| Capture safety | Requires SDL_AUDIODRIVER=dummy before audio initialization, asserts actual dummy driver afterward; isolated XDG preferences, no physical microphone |
| Offer/capability | Native publisher's actual serverinfo offer retained at existing held reliable-send boundary, full client parser produces capability bytes and actual server command consumer admits each peer |
| Encoder and sender | Deterministic controlled20ms PCM, prepared desktop PTT preference and real dummy capture device, native PTT key/capture-frame/VAD/meter/Opus producer, client queue and actual CL_SendMove serialization |
| Talk-burst completion | Native PTT release followed by controlled capture frame produces END; actual sender/server consumer/relay deliver it |
| Receiver and output | Actual server voice queue/relay and full client packet parser invoke native generation/jitter/Opus decode, Voice_Frame buffers PCM; Voice_MixAudio produces nonzero16-bit stereo output and consumes observed buffered ring frames |
| Mute | Nonzero unread native PCM is verified before numeric-slot voice_mute; second buffered burst mixes silent while consuming its ring |
| Retirement | Third/fourth native bursts leave nonzero unread PCM and pending jitter; an actual producer packet remains queued. Native transport reset/Voice_ResetConnection then clear capability/outgoing queue and receiver generation/jitter/PCM; Voice_Shutdown retires initialized/encoder/capture state |
| End of fixture | Existing native NET_FreeQSocket retires driverless test descriptors, native client-drop retires active/netconnection and QC releases before host shutdown. Prepared resources stay alive through native disconnect; only afterward aliases clear and resources free; process exit0 |

No voice protocol, capability, generation, packet or decoded-audio values are
assigned. Client signon/resources, DMA, desktop preference and capture PCM are
explicitly prepared seams. Unreliable bytes are captured, socket sequences do
not advance, and two client states use one process/global voice owner. This
proves the stated native codec/queue/parser/mixer boundaries, not separate live
clients, connected timing, real microphone or hardware output/listening quality.
VAD processing/meter is exercised under PTT; sensitivity/preroll/VAD-mode matrix
is not certified. Default/failing/exact-device routing, saved VR-on/opt-out and
desktop opt-in, gain/loss/reordering/budgets/newer/older generations/map/slot
recovery, HRTF/native fallback/spatial/music remain distinct F08 work.

Main corrected fixture assumptions at existing boundaries before accepting a
run: command warmup, initial serial, buffered-frame drain, capability-buffer
lifetime, offer retirement after spawn, burst END delivery and synthetic endpoint/
QC cleanup. Native dropped clients retain old capability fields; active and
null-netconnection guards retire them until ordinary slot initialization. No
production change or fabricated immediate-zero policy.
[Before-code plan and adjustments](voice-pcm-final-2.0-plan.md).

Private current evidence: voice-pcm-current/final/{voice-pcm,compile-argv.json,
link-argv.json}, logs/voice-pcm-current-final-build.log and
voice-pcm-current-final-run.log; both actual build/run exit0. The revised
reviewed build/run is separately retained. Assertion-disabled builder refuses
with its explicit DEBUG requirement; textual source compile refuses at the
explicit NDEBUG guard. The first source-negative attempt reached a PCH mismatch
before the guard, so only the later textual compile proves that guard. Prior private
build adapter and reproducible outputs are retained separately. Earlier link-order/offer/jitter/
synthetic-shutdown failures and native debugger traces remain separate; none
are passing evidence. Licensed pak0 is a read-only link in a disposable basedir;
main/reference/user settings/assets and deployed servers are untouched.

Focused local Astra xhigh review identified the fixture lifetime bug and
missing reset/mute preconditions. Main spot-checked and adopted those fixes;
newer/older generation ordering remains explicit distinct required F08 work.
[Disposition table](voice-pcm-final-2.0-plan.md). Verified Astra xhigh follow-up finds no remaining concrete issue in the revised
fixture. The builder and source explicitly reject NDEBUG/assertion-disabled
compilation so dummy/input guards and test assertions remain effective; overall F10 signoff
and whole-goal acceptance remain open. This completes only the stated F08 subset.
