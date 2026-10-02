# Native voice byte budget, busy retry and fatal cleanup

2026-10-02. Frozen F08 subset; no production edit. [Byte/busy plan](voice-budget-final-2.0-plan.md), [fatal cleanup plan](voice-fatal-send-final-2.0-plan.md), [verified senior brief](voice-transport-final-2.0-review-brief.md). Local Astra/xhigh review is complete with the dispositions below, not final F08/F10 acceptance.

First complete combined -vad -routing -recovery -budgets -fatal-send run: existing native fixture builder0, native run0, completion0 and all six pass markers, normal Host_Shutdown. Current assertion-enabled debugoptimized Meson objects are up to date, SDL3/voice/Opus enabled, Steam Audio disabled. Private receipt root is /home/obesecatlord/FastGames/qsvr-voice-budget-egp46emq: actual compile/link/build/run argv, logs, six-marker exits and current source hashes. Fresh XDG preferences and read-only licensed pak0. No partial patch executed; no GPU/OpenXR/hardware/microphone/deployed server/user preferences touched.

| Boundary | Observed result and exact limits |
| --- | --- |
| Large valid codec payload | Native getters save bitrate/VBR/DTX; actual encoder setters prepare160kbps CBR/no-DTX. Deterministic noise uses actual Voice_EncodeCaptureFrame/CL_SendMove; >=300-byte payloads traverse actual server parser. An independently created Opus decoder yields960 samples with nonzero audio from an accepted packet. This codec configuration is prepared test input, not a new game setting/default. |
| Byte refusal | Unique producer sequences avoid duplicate rejection. Before75packet limit, first packet exceeding16000bytes/window leaves serial/generation/packet count/byte count/window/source history and recipient relay state unchanged. Accepted packets increment exact serial/count/bytes and preserve encoded content. Source remains connected. |
| Next-window recovery | Retained actual refused datagram is manually redelivered after controlled realtime advance. Native parser accepts it, resets counters to one/exact charge and keeps source generation. This is admission recovery, not automatic reliable retransmission. Native END is sent, actual codec settings restored, and ordinary native relay expiry advances past high-payload history. |
| Busy send0 | Native producer creates ordinary speech/END. Wrapper returns0 only around actual SV_SendPendingVoice; native framed bytes are attempted, recipient remains active, source serial/generation and recipient cursor/generation/rotation stay unchanged. |
| Accepted retry1 | Second actual relay call with wrapper1 emits identical bytes, commits cursor and scheduler rotation. Native receiver parser/jitter/decode produces nonzero PCM and actual mixer consumes it; snapshot is saved after playback. |
| Fatal endpoint construction | Old driverless receiver retires through its existing pre-free/drop pattern with sender aliases live. Actual Loop_Connect creates mutually linked driver0 sockets under its constructor context; NET_CheckNewConnections/SV_CheckForNewClients admits server endpoint. Existing native offer/header/stock-QC spawn/voice handshake owners run. Retired receiver resources free after aliases move to fresh state. Dedicated held Loop_Init remains a test seam. |
| Fatal send-1 | Actual relay attempts framed data, returns false and invokes native SV_DropClient(false), stock QC disconnect, NET_Close/Loop_Close/Free. Recipient is inactive/null connection, server socket disconnected, paired client unlinked and active connection count decreases. Sender remains active with unchanged generation/serial. Actual NET_Close retires paired client after aliases move to sender. No synthetic close or socket driver override. |
| Reused recipient | Existing fixture SpawnPeer/voice handshake replaces the retired slot. Native relay cursor starts at current source serial, excluding prior queued audio. New speech traverses native relay/parser/jitter/PCM and mixer, then sender/receiver snapshots and transport default success are retained for following checks. |
| Consolidation | Previous VAD/preroll/content-class/meter/PTT/direct reset, stored preferences/routes, codec/mute, pressure/rate/gain/loss/reorder/generation and final queued-PCM/jitter/sender reset/native shutdown checks all pass in the same process. |

Luna/xhigh wrote the byte/busy header and minimal existing-wrapper/optional integration with non-overlapping ownership, source-only verification; main verified effective context and complete code, then closed worker. Main implemented the separate fatal header and optional integration after worker ownership ended, reusing native constructors and existing helpers. No production state machine, transport, codec or test builder is replaced.

Successful/busy/fatal transport results are explicitly controlled in the inherited capture wrapper. These runs do not establish independent socket delivery, OS send failure, real-time cadence, physical microphone failure/detection/retry, full client reconnect/map reset, active XR profile, HRTF/spatial/music or listening quality. Prepared core client resources/signon and synthetic DMA remain seams. Actual source-slot generation was qualified separately; fatal recipient slot reuse does not substitute for complete receiver connection/map-reset behavior. Other F08 and F10 boundaries remain open. No performance measurement or claim.

## Senior dispositions and final provenance gate

Main verified effective local gpt-6-astra/xhigh review context. Reviewer checked
all eight fixture-source hashes and actual six-marker exits; no P0/P1 code
findings. Main independently inspected the cited byte-charge-before-duplicate
branch, cursor commit, native pooled-socket Close/Free and allocation alias
ordering. No source edits were needed after the first passing run.

| Recommendation / limit | Main disposition |
| --- | --- |
| P2 stale entry scope excluded fatal cleanup | Adopted. Original metadata/first complete run remain under first-integrated-run; current entry.json records the actual consolidated fatal case and preserved seams. |
| P2 brief claimed every decoded sample nonzero | Adopted. Count is960 decoded samples containing at least one nonzero signal sample; no per-sample claim. |
| Busy cursor preservation has normalization/expiry preconditions | Accepted. Tested current-generation, unexpired history; native generation/overwritten-cursor normalization and expiry may advance state before a busy send. No universal immutability claim. |
| Native loop constructor/close adapter and alias ordering are sound | Accepted. Pooled socket storage permits post-close disconnected checks before any reuse; native driverdata unlink and active count are observed. No new transport/lifecycle owner. |
| Original source hashes were post-run and omitted common objects | Addressed for the execution interval. Main added actual compiler dependency, linked object/library, PCH and executable hashes before/after a final consolidated run. All captured bytes remain stable, source hashes still match, and all six markers/exits pass. This does not establish a fresh from-source rebuild of every reused common object or final shipping artifact. |
| Optional busy-capture helper consolidation | Deferred. Existing captured bytes/results are needed around temporary0 as well as1; bounded test boilerplate remains within one fixture, no new helper/service required. |

Final build/run/completion/input-stability exits all0. Pre/post inputs and
reviewed-provenance-summary.json are retained in the private profile. The
first integrated run is preserved separately. Final F10 matching shipping
rebuild/artifact reconciliation and integration senior signoff remain required.
