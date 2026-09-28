# Astra brief: selected arrival gaps

Solo game fork; no enterprise ceremony. Review the bounded preimplementation
plan [here](predictive-arrival-gap-2.0-plan.md). Need the smallest reuse of the
pause/resume fence that keeps a valid quiet player connected without stale
input, false completion or a parallel physics owner.

## Environment and evidence labels

| Fact | Status / location |
| --- | --- |
| Linux vkQuake worktree, branch `2.0`, baseline `0b1718a3` | Verified previous branch check and clean source at this draft. Root `/home/obesecatlord/Documents/quakespasmvr/quakespasm-2.0`. |
| `docs/migration-2.0.md` has unrelated user edits | Verified status; read only, do not edit/stage. |
| Stock admitted selected driver passes native transitions and full message/replay checks | Verified preceding implementation logs and `tests/mixed_native_fixture.c`; this does not prove network reliability. |
| Both receipt and quiet-frame paths fatally reject RUNNING live selected gaps >1s | Verified `sv_user.c` near 1367 and 2020. |
| Existing phase machine and marker can clear transient input without completing ACK | Verified `SV_PrivateSyncPauseState`, `SV_HandlePrivateResumeMarker`, selected completion tails in `sv_phys.c`. |
| Corpse exemption is only used by old fatal-gap checks plus unreachable nested unselected logic | Verified `rg private_move_resume_pending`; investigate removing it, not repurposing it. |
| VR pending clear does not reset roomscale/contact producer continuity | Verified `CL_PrivateMoveResumeObserved` versus `VR_InputInvalidateMotion`; resulting behavioral requirement still unverified. |
| UDP sockets and ptrace unavailable in sandbox | Verified failures earlier. No bypass, new transport or production test hooks. Use actual-code native fixtures with captured delivery. |
| No live headset/eye tests, Windows/ARM qualification or benchmarks required now | User explicitly deferred/excluded them. |

Read the real source before critique; source line numbers may shift. Current
code is authoritative over these assertions. QSS-M reference is sibling
`../QSS-M/Quake/sv_phys.c` and `sv_user.c`, not the inherited fork's generic
predictive solver. Do not re-review unrelated renderer/foveation/model work.

## Decisions and current lean

1. Extend the existing phase synchronizer for RUNNING and stalled
   AWAIT_COMPLETION live owners; clear once and publish a fresh GAP epoch.
   AWAIT_MARKER remains idempotent. Reject a second timeout state/protocol.
2. Delete the corpse exemption when fatal gaps are removed; terminal owners
   do not enter a gap fence while dead. Re-evaluate on returning alive.
   Reject carrying an indefinite post-respawn held-input exemption.
3. Preserve physical state/timers at the fence and existing no-command
   dispatcher. Do not add world-clock WALK physics solely because packets
   stopped; QSS-M already uses command ownership for predicted owners.
4. Reuse `VR_InputInvalidateMotion` only if required for coherent fresh
   tracking. Its neutral-stick gating is an explicit cost; assess whether the
   existing pending clear plus server baseline reset is sufficient instead.
5. Teleport before marker must keep handshake working through the existing
   epoch/reason metadata. Avoid a new reason owner; flag any real overlapping
   relocation loss the minimal proposal creates.

Decisions 1/2 overlap; merge if simpler. Prioritize the failure with highest
correctness impact and deep-spec that, rather than expanding all five. Return
at most 1100 words: verified evidence, ranked issues, concrete revised contract,
test omissions, genuinely human decisions (if any). Read-only ownership; no
delegation/production edits. Verify effective Astra/max settings first, using
only model/effort `turn_context` fields; do not export raw session telemetry.
