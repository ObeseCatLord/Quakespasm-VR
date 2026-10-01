# Retire capture continuity on dropped or failed microphone data

2026-09-30. Before-code AUDIO-005/010 capture-continuity brief, baseline
b819048e. No production changes for this slice yet. Keep SDL capture, native
voice settings/profile, Opus/VAD, network queue and spatial self-feed owners.

## Verified source and official contracts

The inherited primary51b452c018273647dcf94f4628a370267ff8fa91 voice/sbar code
already supplies permission/transmit/meter/speaker HUD values. Current2.0
Sbar_DrawVoiceStatus reads atomic snapshots; Host_Frame -> S_Update calls
Voice_Frame once, independent of per-eye draws. Existing native GUI/panel
owners render it for desktop and VR. No second HUD update/capture owner needed.

Current Voice_ProcessCapture clears input level and SDL data on negative
availability or converted backlog, then returns without clearing voice_sending,
voice_transmitting, preroll, VAD or Opus continuity. A previous successful talk
can therefore remain LIVE indefinitely during repeated stream errors and later
reuse preroll from before discarded input. A short/error read merely breaks.
SDL2's raw backlog/conversion-failure branches also clear data and return0,
hiding the discontinuity from this consumer. These are actual branch/state
gaps, not a missing symbol inferred from the index. Primary raw backlog also
drops data without resetting that history; new converted-error paths need an
explicit failure policy at the same owner.

Official SDL3 [available-data documentation](https://wiki.libsdl.org/SDL3/SDL_GetAudioStreamAvailable)
permits zero while buffering and returns-1 on failure. Its
[read contract](https://wiki.libsdl.org/SDL3/SDL_GetAudioStreamData) returns the
number actually read or-1; requested length is a maximum. SDL2's
[read contract](https://wiki.libsdl.org/SDL2/SDL_AudioStreamGet) has the same
error/actual-length distinction. Do not treat ordinary no-data/partial buffering
as missing permission or reopen a device every host frame.

## Minimal reuse and alternatives

Refactor the existing Voice_StopTransmit reset body into one private helper
with an explicit release-PTT boolean; keep Voice_StopTransmit as the true
wrapper for all existing settings/session/stop callers. Capture data-loss
paths use false: clear queued outgoing speech, atomic sending/meter, VAD,
preroll and encoder continuity; retain held physical PTT key state and saved
permissions. Reuse the existing best-effort END after a previously active
talkspurt. This prevents a normal hitch requiring release/repress to resume.
Do not copy another stop/reset body or introduce a parallel capture state.

Use one private negative sentinel for deliberately discarded SDL2 raw backlog,
distinct from SDL API failure. The consumer must reset discontinuity for that
sentinel and for the existing converted-backlog limit, retaining an otherwise
healthy capture device. SDL2 dequeue/put failures propagate actual failure
instead of hiding it as0. Clear SDL buffered data and reuse Spatial_ResetSelf
on discontinuity so wet-only local monitoring cannot replay pre-drop PCM.

For genuine negative availability/read errors, reset continuity and close
capture through existing Voice_CloseCapture; retain desired route/preferences
and use existing next-device-check cooldown for bounded retry, not a new retry
state machine. That clears capture-ready/NO DEV state. Reuse the existing
10-second failed-open delay. Normal zero availability leaves state untouched.
A positive short read after a full-frame availability query is discarded
with continuity reset, without claiming device failure or opening another PCM
assembly buffer; the old path already discarded such partial data.

Keep successful full-frame encode behavior and the20ms frame/network/jitter
contracts. No auto fixed foveation/device routing/prompt/persistent setting
changes, packet protocol, extra lock/thread or sample-rate rewrite. System
default microphone, saved VR default-on opt-out, desktop opt-in and independent
wet-only consent remain. Existing saved opt-out cannot be changed by errors.

Expected production write set Quake/voice.c only, roughly50–90 added/helper
lines after the adopted terminal-marker correction. Reopen if correctness requires another capture/queue owner, partial-frame
assembly or wider DSP/device migration. Review error versus backlog distinction,
PTT preservation, END/reset ordering, SDL2/3 differences and self-feed lifecycle
before implementation. Main verifies findings and records disposition, then
delegates coding and reviews the full patch with final bounded Astra advice.

## End-of-implementation qualification

Actual capture-to-VAD/Opus-to-native queue and HUD must cover ongoing speech
followed by raw/converted backlog, query/read/conversion error, short reads and
ordinary0/sub-frame availability. Dropped input must not retain LIVE/stale
preroll; healthy data resumes a fresh talkspurt, including held PTT and release
during retry. Failure closes capture and retries with existing cooldown; saved
opt-out/default device stays intact. Include VR/desktop profile switches,
no-network wet-only monitoring, actual spatial reset and no-Opus fallback.
Software fault injection supports real owner coverage, not audible/headset proof.
No builds/tests/compiler/probes/fixtures/game runs before all implementation.
Linux/ARM qualification comes last; Windows and user listening trials deferred.

## Before-code Astra disposition

Requested local Astra xhigh source advice confirmed the gap and adopted reset
reuse with two P2 corrections. Main checked official SDL contracts, native
four-packet client drainage/validation, and Spatial_ResetSelf callback exclusion
before adopting the bounded design. No runtime or effective-model certification.

| Recommendation | Disposition |
| --- | --- |
| Reuse one reset body with explicit release-PTT parameter and existing true wrapper. | Adopt. Data loss uses false; preserve packet sequence/timestamp/talkspurt counters. Valid resumption already increments talkspurt and emits START. No profile lookup inside the reset body, avoiding recursive profile-sync entry. |
| Treat every nonnegative short read/dequeue0 as failure/discard. | Correct P2. Normal read0 breaks without reset. Only positive incomplete read discards consumed partial data and retires continuity. SDL2 unsigned dequeue0 is not a signed API failure: stop feeding raw data and preserve already converted data. Conversion-put failure remains distinguishable. |
| Repeated discard can clear an undrained END. | Correct P2. Extend the existing private queue-clear helper to preserve pending zero-payload END markers when data-loss reset occurs with no active sending; discard speech in-place in bounded circular order and preserve exact packet fields. Existing full-stop/reset callers clear normally. Active sending produces its normal fresh END. No fresh timestamp, replay queue or ACK state. |
| Close only genuine failures and explicitly install cooldown. | Adopt. Use existing CloseCapture and next-device-check=realtime+10.0; retain desired-route/session/preferences so retry does not trigger a PTT-clearing route change. Normal backlog keeps its device; release during retry remains processable. |
| Clear local wet-only PCM through native spatial helper. | Adopt. Main verified Spatial_ResetSelf excludes callback using existing SNDDMA lock/submit and resets the existing self ring/effect. Closure already calls it; healthy discard calls it separately outside playback-lock regions. No new sound lock/worker or consent changes. |
| Add a no-data timeout, partial-frame assembler, or another retry owner. | Reject unproven adjacent scope. Freshness claims cover detected errors/discards only; ordinary zero data can retain prior LIVE state. Existing error/device checks and user software qualification remain. |

One private discard helper may combine existing capture clear/reset and
healthy-self-reset versus failure-close/cooldown, avoiding duplicated branch
policy. SDL2 raw-backlog sentinel is checked before generic negative errors.
Production remains voice.c only. Final exact-patch source review and full
Linux/ARM capture/HUD software qualification remain after implementation.

## Source integration receipt

Production commit `47df36e6` changes voice.c only:73 additions and20 removals.
The existing reset body has an explicit PTT-release parameter; full-stop callers
retain their old wrapper. Detected capture drops/errors use the shared discard
helper. Inactive repeated drops preserve exact pending END markers by native
four-slot circular compaction; successful recovery uses existing START/talkspurt
behavior. SDL2 deliberately dropped raw input is distinct from true errors;
ordinary zero data is unchanged. Genuine failures close and retain the existing
route/preferences with the explicit10-second retry gate; healthy drops retain
capture and reset native wet-only self-feed.

Main reviewed the complete patch and actual queue/drainage/spatial owners.
Independent requested-Astra final source advice found no P1/P2 issue within
scope, including wrapped compaction, PTT/terminal-marker lifecycle, SDL branches,
callback exclusion and cooldown. Main also checked the official SDL2
[conversion-put failure contract](https://wiki.libsdl.org/SDL2/SDL_AudioStreamPut)
and [unsigned dequeue result](https://wiki.libsdl.org/SDL2/SDL_DequeueAudio).
Scoped diff --check passed. No builds/tests/compiler/probes/runtime/audio checks
ran. Effective reviewer metadata and final Linux/ARM software qualification
remain unverified. No no-data freshness timeout or audible benefit is claimed.
