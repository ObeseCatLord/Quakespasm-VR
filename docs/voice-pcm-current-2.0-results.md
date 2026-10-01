# F08 controlled PCM and native voice relay results

2026-10-01. Current preliminary Linux DEBUG Meson engine through production
aa828629; SDL3, USE_VOICECHAT and actual Opus enabled, Steam Audio disabled.
Strict compiler/link flags retained without warning suppression. New261-line
fixture reuses existing mixed/negotiation bootstrap and includes native voice.c;
no production edits or replacement voice/transport/codec owner. Main verified
two effective Luna gpt-6-luna/xhigh contexts and reviewed/integrated all output.

The final-drop native run and reproducible-builder run both exit0 with
VOICE_PCM_NATIVE_PASSED. The40-line voice-specific builder reads the primary
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
| Mute | Native numeric-slot voice_mute command makes second buffered burst mix silent while consuming its ring |
| Retirement | Native transport reset and Voice_ResetConnection clear capability/outgoing queue and receiver generation/jitter/PCM ring; Voice_Shutdown retires initialized/encoder/capture state |
| End of fixture | Existing native NET_FreeQSocket retires driverless test descriptors, native client-drop consumer retires active/netconnection and QC owner releases before normal host shutdown; process exit0 |

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

Private evidence: voice-pcm-current/{build.py,compile-argv.json,link-argv.json,
production-input.json,reproducible/}, logs/voice-pcm-current-build-final-drop.log,
voice-pcm-current-run-final-drop.log, voice-pcm-current-reproducible-build.log
and voice-pcm-current-run-reproducible.log. Earlier link-order/offer/jitter/
synthetic-shutdown failures and native debugger traces remain separate; none
are passing evidence. Licensed pak0 is a read-only link in a disposable basedir;
main/reference/user settings/assets and deployed servers are untouched.

Focused local Astra source acceptance review is pending; overall F10 signoff
and whole-goal acceptance remain open. This completes only the stated F08 subset.
