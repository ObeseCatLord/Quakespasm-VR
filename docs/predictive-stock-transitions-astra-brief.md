# Astra brief: selected stock native transitions

Read-only architecture review, solo-maintainer scale. Review the preimplementation
plan in `predictive-stock-transitions-2.0-plan.md`, not the full migration. Main
has verified each fact there by reading the named symbols. Verify load-bearing
claims against source before critiquing. Do not edit files or widen into a
generic mod physics rewrite.

| Environment | Fact |
| --- | --- |
| Workspace | `quakespasm-2.0`, branch `2.0`; main/product branch must remain untouched. |
| Reference | Current branch native dispatcher; read-only sibling vkquake and QSS-M available if needed. |
| Program | Initial selection pins stock size 340014/CRC 0x0bf8/hash 0xcf69c3e2. Mod admission is a later stage. |
| Defaults | Private transport on (`10cbbc62`); selected PMove remains off. No activation change proposed here. |
| Tests | Actual QC/map native and mixed captured-transport fixtures exist. No graphics/socket proof from them. |
| Constraints | Sandbox denies socket creation; Linux offline checks available. User defers live/hardware, Windows/ARM and measurement. |
| Dirty file | `docs/migration-2.0.md` is user-owned and must not be touched. |

Open decisions (main's lean, not established conclusions):

1. Is frame-start stock NOCLIP/FLY adaptation of the existing terminal coalescer
   the smallest sound change? Lean yes; extending solver modes changes native
   cheat behavior, deselection duplicates queue/mode transitions. Challenge
   whether keeping selection latched actually introduces more policy.
2. Can a single narrow classifier align receipt/physics/snapshot without hiding
   invalid finite/hull/ground/QC identity conditions? Lean distinguish valid native
   states from malformed/unsupported ones; do not return a generic success for
   every native-dispatched state.
3. Which callback/time/replay boundaries must block stage-2 activation? Lean
   native dispatch is chosen before selected callbacks; no full-world native
   restart after partial command consumption. Identify actual stock-reachable
   transitions versus later mod scope. Existing terminal continuation is not
   automatically safe for living callbacks.

These decisions overlap; merge them if useful. Rank by leverage and deepen the
highest-risk issue. Output <= 1100 words: verified/corrected claims with file:line
evidence; prioritized recommendations and concrete acceptance additions;
architecture simplifications; any genuinely human decision (none expected).
If evidence is missing, say so rather than broaden the review. No delegation.

Not-list: no graphics/foveation/platform review, new protocols, whole-QSS-M port,
release qualification veto based only on deferred headset playtesting, generic
security review, or implementation. You are not alone in the codebase; main may
write planning docs while your source review is read-only.
