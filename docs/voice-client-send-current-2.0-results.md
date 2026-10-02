# Native client voice send and disconnect results

2026-10-02. Frozen F08 subset, no production changes. [Before-code plan](voice-client-send-final-2.0-plan.md) and [verified senior brief](voice-client-send-final-2.0-review-brief.md). Local Astra/xhigh review and main dispositions are complete below; this is not final F08/F10 acceptance.

The existing native builder and consolidated -vad -routing -recovery -budgets -fatal-send -client-send run finish build/run/completion/input-stability0 with all seven markers and normal Host_Shutdown. Assertion-enabled debugoptimized native objects enable SDL3/voice/Opus/codecs/CURL; Steam Audio is disabled. Private receipts: /home/obesecatlord/FastGames/qsvr-voice-client-7a6iijpg. Fresh XDG preferences, linked licensed pak0 used only for reading, dummy audio, dedicated/no UDP/no sound/no Steam API. No GPU/OpenXR, hardware microphone, deployed server or user preferences touched.

| Boundary | Observed native behavior and limits |
| --- | --- |
| Public heartbeat busy0 | Real bound-PTT/native encoder queues valid START speech. Actual CL_SendMove(NULL), public protocol, controlled send0 preserves queue head/count/storage and connection. Exact attempted header/payload read with native MSG readers; refused bytes are not delivered to server. |
| Accepted retry1 and END | Next actual heartbeat consumes that packet; actual server parser records exact sequence/timestamp/talkspurt/flags/active payload. Bound PTT release/native producer makes zero-payload END and subsequent heartbeat delivers it. Actual relay/full client parser/jitter/decode produces nonzero PCM; native mixer consumes it. Receiver snapshot saved after playback and sender/netcon restored. |
| Fatal input witnesses | Existing final phase leaves unread nonzero decoded PCM, pending jitter/generation and independently produced queued sender speech. Other driverless peer retires through existing pre-free/drop. Actual Loop_Connect under saved/restored constructor driver0 and Loop_CheckNewConnections create paired native endpoints, associated with already-admitted sender/prepared client. Replaced driverless sender pool-freed. This is prepared transport association, not fresh native admission or full reconnect. |
| Fatal client send-1 | Existing capture wrapper returns-1 for actual CL_SendMove(NULL). Scoped default-false keep-first guard preserves the attempted voice datagram when native disconnect also sends clc_disconnect. Existing client caller runs outside QC VM; native common CL_Disconnect retires transport/voice, closes client and hosted server. Native stock-QC drop/NET_Close/Loop_Close retire server peer. Exact attempted packet is checked; no failed data delivered. |
| Teardown | Both paired endpoints disconnected/unlinked; cls disconnected/null socket, sv inactive, peers inactive/null and active count0. Sender queue/head/protocol and capture/wanted/PTT/VAD/preroll/meters/transmitting retire; speaker0 generation/jitter/PCM retire. Saved transmission consent/mode stay unchanged. Already-inactive final cleanup avoids duplicate drops; allocations remain until normal native Host_Shutdown finishes cache writes, then free. |
| Provenance | 347 actual compiler-dependency/link/PCH/executable/runner/licensed-pak0 inputs, including217 linked objects and1 PCH, are byte-stable before/after execution. Nine source hashes still match. Executable SHA2562f313e458f97731fbad0870dcff6c9d440a93ea6346047da555494d23421d8b5. Actual argv/logs/snapshots and reviewed-provenance-summary.json retained; not a fresh rebuild of every common object or final shipping artifact. |

Luna/xhigh implemented bounded header/integration with source-only verification, closed before main execution. Main reviewed complete source, saved receiver state after playback, and fixed three fixture failures while retaining each original source/receipt: undefined declared MSG_ReadData replaced by existing byte reader; invalid !msg_badread-at-EOF requirement corrected to native exact-consumption convention; server-QC-selected client disconnect corrected to native client caller context. No production behavior changed.

Controlled send results, held Loop_Init/reliable-send wrappers, prepared core state/signon/virtual DMA and dedicated hosted-client role remain seams. This does not certify independent OS/socket failure, private movement/ACK framing, fresh client reconnect/map reset, active XR profile, physical/device-read failure and retry timing, HRTF/spatial/music/listening or performance. Previously qualified server fatal admission/recipient reuse is distinct. Remaining finite F08 boundaries and final F10 integration/artifact gates remain open.

## Senior dispositions

Main verified effective local gpt-6-astra/xhigh context. Reviewer found no P0/P1;
main independently inspected native PTT key/reset, socket free-list and host
client/QC ordering. Original passing run/receipts are preserved under
pre-senior-passing-run; all three earlier failed attempts remain separate.

| Recommendation | Disposition |
| --- | --- |
| P2 held-PTT reset was not witnessed | Adopted. Optional fatal case now leaves native bound PTT held after encoding. Fatal header first asserts voice_ptt, voice_ptt_keys[F12], sending, published transmitting and positive input meter. Actual disconnect then clears both PTT aggregate/key and other existing reset witnesses. Ordinary fixture release behavior stays unchanged. |
| Recycled replaced socket is not independent retirement evidence | Adopted. Check pool retirement immediately after free, before Loop_Connect can recycle it; remove redundant post-close replaced assertion. The two current paired-endpoint checks remain. |
| Linked writable pak0 omitted from input provenance | Adopted. Final runner hashes resolved licensed pak0 and runner before/after alongside native inputs; bytes stable. Link is used only for reading, with no enforced filesystem read-only claim. |
| Native association/caller context/EOF and allocation lifetime are sound | Accepted with prepared-core/dedicated/controlled-transport limits above. No production architecture change or second lifecycle owner. |

After the required witness/provenance edits, one affected consolidated rerun
again finishes all four statuses0, seven markers and normal shutdown. Final
347-input/217-object/1-PCH/nine-source hashes independently checked by main.
This is the corrected bounded result; original review inspected the earlier
passing source, and main applied/verified its explicit corrections. It is not
final whole-goal Astra integration signoff or a current shipping artifact pass.


Later fixture integration adds optional fallback spatial cases; this earlier
nine-source hash snapshot qualifies its own a28a76bb input. All seven cases pass
again on the new fixture together with fallback spatialization; use the
[eight-marker current results](voice-fallback-current-2.0-results.md) for latest
fixture provenance. Production shipping bytes remain unchanged.
