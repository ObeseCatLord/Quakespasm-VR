# Optional VRIK and voice capability retry

2026-09-30. Before-code plan; baseline `226eafc4`. Repairs demonstrated
AV-001/AUDIO-004 negotiation loss at the existing client reliable-buffer
boundary. No wire, codec, server, prediction or capture policy change.

## Verified source evidence

- [verified: actual offer parser and whole-Quake callers] `cl_parse.c`
  `CL_OfferVRIKProtocol` consumes a valid offer but returns before latching
  anything when `cls.message` has insufficient space. None of its three
  offered versions is retained for later retry. `CL_OfferVoiceProtocol` has
  the same loss for `voice_cap 1`. Neither is retried by `CL_SendCmd`.
- [verified: existing native owner] `cl_main.c:CL_SendCmd` already retries
  numeric/custom avatar capability and selection before building movement
  and sending the existing reliable buffer. Its helpers retain pending state
  until an entire opcode/string/NUL fits. This is the reusable scheduling
  and buffer owner; no new sender or acknowledgment channel is needed.
- [verified: current semantics] VRIK's codec latch selects the first supported
  offered version once; serverinfo offers 4, 3, 2. Current offered/version/sent
  flags are committed only when the reply is appended, or when the existing
  playback branch latches without sending. Voice receive accepts only the
  established offered state; outgoing voice additionally requires sent state.
  Preserve those admission semantics rather than treating a pending offer as
  an active capability. VRIK/voice are independent of public/private movement.
- [verified: lifecycle] `CL_ResetVRIKState` and
  `CL_ResetVoiceTransportState` are actual serverinfo reset owners; client free
  clears the native client state. Pending replies must retire with those owners.
- [unknown] Executable full-buffer signon, reliable draining, mixed-peer voice
  and pose behavior. No execution or test has run for this checkpoint.

## Smallest adapter and alternatives

Retain the first valid supported VRIK offer in one pending-version byte, and a
voice offer in one pending boolean. Their distinct requested-versus-active
state is demonstrated by a full reliable buffer; it is not a second protocol.
Reuse native `CL_SendCmd` for retry. Extract each existing reply/latch block
into a bounded helper, call it immediately from offer parsing (preserving
ordinary timing) and subsequently from `CL_SendCmd` alongside existing avatar
retries. Clear pending only after the complete reply is appended and flags
committed. If disconnected, invalid/full/overflowed buffer, retain the pending
intent until normal capacity or reset. Do not reduce movement/precaches, flush
an extra packet, wait for ACKs or reset a buffer to create space.

Preserve first-supported-offer precedence while pending: later supported lower
offers and duplicates do not overwrite the choice; malformed/unsupported exact
tokens remain consumed and cannot enter console execution. Existing playback
latching writes no reply and adds no VR demo feature. Actual active offered,
selected-version and cap-sent fields stay unchanged until successful latch.

Alternative: reuse active offered/version fields for pending state. Rejected
provisionally because it changes receive admission before a reply fits and
requires changing adjacent generation/voice parsers. Alternative: generically
rewrite every optional negotiation as one service. Rejected because existing
avatar/VRIK/voice owners already define their independent contracts. Two small
reply helpers at the native buffer boundary are sufficient.

Expected write set: `Quake/client.h`, `Quake/cl_parse.c`, `Quake/cl_main.c`.
Target fewer than 150 added lines with deletion of the two original reply
blocks. No `cl_input.c`, server, codec, voice/audio or renderer edits. This is
disjoint from the active three-file alias-root worker. Request a bounded local
Astra review of pending-versus-active semantics and exact lifecycle/send order,
record disposition, commit plan, then delegate implementation. Main reviews
all edits. Reopen if a new sender/state machine or additional module is needed.

## Final software acceptance

After all implementation, exercise actual offer parser, `CL_SendCmd`, native
reliable send and server capability consumer: both offers while buffer is full,
subsequent drain/retry, capacity exactly sufficient versus one byte short,
single opcode/string/NUL, no premature active/sent flags or pose/voice sending,
first supported 4/3/2 choice, duplicates/malformed/unsupported offers, independent
VRIK/voice readiness, map reset/disconnect before retry and ordinary avatar
retries/movement retained. Preserve existing native desktop playback behavior
without adding VR demo qualification. Helper-only tests or queued byte counters
do not prove the real negotiation round trip. Linux and ARM checks follow full
implementation; Windows/live multiplayer/headset/microphone tests remain deferred.

## Before-code Astra source-review disposition

Main verified the returned load-bearing scheduling and reset claims against
the actual owners. Requested local Astra xhigh source advice is not certified
model/runtime evidence; no executable or full-goal signoff is claimed.

| Recommendation | Disposition |
| --- | --- |
| Define sent as completely enqueued, not delivered or ACKed. | Adopt. Preserve existing unreliable-before-reliable send order and early-pose server handling. Only pending offers must remain inactive; successful enqueue may precede network delivery. No extra ACK gate. |
| Put independent retries after existing avatar trio, before movement. | Adopt. No SIGNONS requirement; one blocked helper cannot block the other or normal draining. |
| Preserve active receive admission and first-supported choice. | Adopt. Separate pending byte/boolean; freeze upgrades as well as lower versions while pending. Existing selected/offered/sent fields change only after whole reply enqueue or playback latch. |
| Exact helper preflight and commit ordering. | Adopt. Live replies require connected state, nonnull buffer storage, valid sizes, no overflow and complete opcode/string/NUL capacity (12 VRIK bytes, 13 voice bytes). VRIK codec latch uses temporaries, append precedes active-state commit, then pending clears. Failed attempts retain intent and active state. |
| Playback rejects retries like the avatar helper. | Reject. Preserve existing playback latch without any network write and without live reliable-buffer requirements; this adds no VR demo feature. |
| Add a missing disconnect reset caller. | Reject stale inference. Main targeted read and HEAD source already contain both reset calls in CL_Disconnect; extend existing reset functions only, with no new caller/reordering. |
| Prove invalid/overflowed buffer recovery here. | Reject broader claim. Retain pending intent; native buffer error/recovery ownership is unchanged. Add final cases for successive drain frames, one reply fitting, blocked reliable send and unusable playback buffer. |

No human decision blocks the bounded repair. The two existing offer grammars,
native reliable sender, avatar retry order and separate movement profiles are
preserved. Implementation remains within the stated three-file contract.
