# F08 directed native voice recovery review brief

2026-10-01. Solo project; preserve existing owners and frozen goal scope.
Focused final senior review of the completed recovery fixture and the evidence
it permits. No production edits in this slice; overall F10 signoff is separate.

| Environment / claim | Verification and limits |
| --- | --- |
| Workspace | Only quakespasm-2.0, existing 2.0 checkout. Main/reference and user-owned migration-2.0.md untouched. |
| Native object graph | [verified: executed/source] Existing 42-line builder derives primary DEBUG Meson compilation/linking, actual Opus/SDL3/native engine, five included owner replacements and existing four wrappers. Steam Audio disabled in this preliminary route. |
| Implementation | [verified: source] Parent initially467 lines plus Luna140-line header. Actual native queue/producer/relay/parser/jitter/Opus/mixer, no voice protocol/packet/generation/counter assignments or copied state machines. Inherited CreateMixedPeerState prepares core client protocol from server state and signon/resources. |
| Input/safety | [verified: source/executed] SDL dummy required before/after audio initialization; prepared private desktop PTT preference, PCM/DMA and two client state structures. Assertions enabled; no physical recording. |
| Delivery | [verified: source] Actual encoded/client serialized/server relayed bytes captured and delivered through native consumers. Existing inherited send wrapper returns controlled successful sends. Single global directed voice owner, controlled realtime, no live socket timing or independent client acceptance. |
| New cases | [verified: executed] Queue overrun retains freshest bounded sequences; stop/end drains. Captured duplicate bytes charge75-packet window without serial advancement; native END refused at limit then accepted in next window. Expired relay retires without output. Gain0/1, reordered/omitted contiguous frames, native source drop/recreate/new generation with old jitter+PCM and old replay rejected all pass. |
| Result | [verified: executed] end-marker-fix build0/run0 with both VOICE_RECOVERY_NATIVE_PASSED and VOICE_PCM_NATIVE_PASSED, normal shutdown. Earlier134 was fixture capture asserting length strictly greater than header; valid END-only native datagram is exactly16 bytes, corrected to >= and retained failed log/GDB. |
| Deferred evidence | [unverified here] Live sockets, byte-rate/high payload/server-pressure/send failure, map reset, full VAD/preroll/meters/discontinuity, saved/default/failed devices/profiles, HRTF/spatial/music. No full F08 closure. |

Evidence root: /tmp/qsvr-final-qualification-thchgzi8/voice-recovery-current;
logs/voice-recovery-current-end-marker-{build,run}.log. Source helpers and exact
case design are in voice-recovery-final-2.0-plan.md. Prior P1 lifetime repair
retains prepared clients through Host_Shutdown; current generation helper frees
retired source only after global aliases use its newly created state.

Decision lean: accept these finite recovery boundaries, keep all deferred ones
open. Incremental fixture adapts existing captured delivery, rather than adding
a second bootstrap/decoder/transport/queue. Challenge false passes, lifecycle
aliasing, implicit source-state changes, rate/reorder/concealment assertions,
and whether source retirement proves only source-slot rather than receiver/map.
Potential overlap: gain and concealment output assertions may need one stronger
per-frame signal observation, not another decoder or another fixture framework.

Read tests/voice_pcm_native_fixture.c, tests/voice_queue_recovery_native_fixture.h,
the builder and only needed native voice.c/voice_jitter.c/cl_input/sv_main paths.
Verify before critique. Read-only; no builds/game/microphone/branch changes or
delegation. Return prioritized concrete findings with file/line evidence and
required minimal fixes, plus accepted/deferred claims, <=700 words. Do not
re-audit overall checklist, architecture, hardware, Vulkan or new features.
