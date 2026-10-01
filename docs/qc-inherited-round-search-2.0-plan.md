# Inherited round-result search compatibility

2026-09-30. Before-code brief for the independently established search omission.
Solo project; copy the inherited predicates at the native find boundary, without
replacing VM/search/entity ownership or expanding mod-specific policy.

## Evidence and environment

Writable checkout is quakespasm-2.0; main and read-only siblings remain untouched,
and user-dirty docs/migration-2.0.md is excluded. No tests/builds/compiler/probes/
fixtures/gameplay/performance work until implementation finishes.

[Verified: pinned primary `51b452c018273647dcf94f4628a370267ff8fa91` actual
pr_cmds.c handlers.] `SV_ShouldSuppressShubRoundResultFind` and
`SV_ShouldSuppressShubCleanupFind` run before `PF_Find` enumeration. They apply
only to active server SSQC on map `shubswager`, searching targetname/2/3/4 fields.
The result predicate latches wincnt/losscnt, rejects opposite win/loss queries,
resets on rounds and rejects the late opposing monster clearer query. The
cleanup predicate rejects win/loss/clearer from matching monster target/target2
when its enemy is the specific cleanup hurt trigger, teleporter or teledeath.

[Verified: current pr_cmds.c.] Those calls and predicates are absent. Native
find currently returns matching entities instead. This is an existing primary
exception covered by the migration, not permission to create more mod adapters.
The neighboring physical-contact find predicate is a separate owner and is not
implicitly included; physical melee remains excluded by the user.

[Verified: native pr_edict.c.] `GetEdictFieldValueByName` already supplies named
field access, with missing fields returning NULL. Reuse it; no new reflection
cache or field registry. Existing VM offsets, string access and edict_size allow
the primary's checked self/enemy conversion to be copied unchanged.

[Verified: native host.c:Host_ClearMemory and sv_main.c:SV_SpawnServer.] Server
spawn calls Host_ClearMemory before progs load; it clears the whole sv structure
after freeing the previous VM. [Verified: primary predicate.] Its result latch
is instead a function-static integer, reset by a rounds query or a find whenever
its active-SSQC-map predicate fails (including an inactive server).
[Unknown: execution.] Whether a direct same-map restart/
save load always makes such a query before a result cannot be inferred from
the predicate. Do not claim executable round/lifecycle parity.

## Worked minimal design and open decision

Lean: copy the donor predicates and required small helpers into pr_cmds.c,
immediately before core find; call them only for SSQC before native enumeration.
Preserve each exact name/field/context/result condition, world return, all native
traversal and all other search builtins. Reuse named-field API; omit the absent
legacy trigger-debug logging/control rather than importing a debug subsystem.

Open latch owner: one integer in existing server_t, cleared by native server
memory lifetime, versus donor function-static reset behavior. Lean server_t:
it supplies a demonstrated existing reset owner and prevents old round results
crossing server replacement without another state machine/generation history.
It is transient round compatibility, not a save-format/settings addition. The
review should challenge this adaptation and any legitimate inherited latch
persistence that would be lost. Exact static reuse is simpler in files changed
but its same-map reset is unproved; no claim that it needs a broad VM rewrite.

Expected ownership after disposition: pr_cmds.c required predicates/helpers and
PF_Find call only, plus one server.h integer if accepted. Approximately 120–180
lines copied/adapted, no new services, generic cleanup policy or asset/QC writes.
Stop/reopen if it needs another round lifecycle/protocol rather than this seam.

Requested local Astra xhigh: verify load-bearing claims and exact primary
predicates, choose latch owner, challenge scope/behavior risks and prefer reuse.
Read-only <=700words; no wider QC/physics audit or execution. Effective metadata
is unexposed, so requested-Astra source advice, not certified/final signoff.

After implementation, final software qualification must use actual loaded QC
for win/loss ordering, repeated matching queries, rounds reset, late opposite
clearer, supported targetname fields, matching/nonmatching monster and cleanup
enemy identity, non-target fields, other maps/programs, CSQC, same-map restart,
server replacement and save load. Return counters alone are not gameplay proof.

## Before-code requested-Astra disposition

Main spot-checked the exact predicate ordering/latch assignment, declared field
APIs, and native fastload-versus-spawning load dispatch. The reviewer challenged
the proposed server-member lifetime: an available reset owner is not evidence
that the inherited lifetime must change.

| Recommendation | Disposition |
| --- | --- |
| Retain the donor function-static latch | Adopt. Delete the proposed server_t member from the implementation scope. Additional map/spawning-load reset would change reference behavior without demonstrated incompatibility, and differ from fastload. Lifetime behavior stays an explicit final software acceptance question. |
| Preserve result predicate before cleanup predicate before enumeration | Adopt. Query itself can latch a result even if no entity is later found. Opposite suppression precedes reassignment; repeated same result remains allowed. |
| Copy exact context/target/enemy predicates | Adopt. No generic monster-cleanup rule or expansion to other maps/search builtins. |
| Reuse the declared field-access composition | Adapt. GetEdictFieldValueByName exists but lacks a public declaration; locally use declared GetEdictFieldValue(ent, ED_FindFieldOffset(name)), its exact native composition, without another header change. Missing fields return NULL; do not claim general bounds validation. |
| Preserve first matching field-definition alias semantics | Adopt. Copy the donor's offset-to-name scan rather than independently comparing named offsets. |
| Remove legacy trigger-debug logging only | Adopt. Keep all behavior surrounding log statements and checked positive/aligned/in-range/non-free self/enemy conversion. |

Final production ownership is **pr_cmds.c only**, copying the minimal helper/
predicate closure and two early world-return checks in PF_Find. Native error,
registry, CSQC, extension searches and existing inactive-client filter remain.
No server member, additional lifecycle owner, asset change or debug subsystem.

## Actual patch source acceptance

Production commit `38cdd7a4`: Luna xhigh copied the accepted minimal helper/predicate closure and two SSQC
early-return checks into pr_cmds.c:195 added lines, no other production files.
Main reviewed the full actual diff and scoped whitespace; requested local Astra
xhigh then independently compared the actual patch with pinned primary and
found no introduced P1/P2 within this scope. Verified field composition/first
alias match, checked self/enemy, exact cleanup context, static lifetime/query
effects, predicate ordering, native traversal and existing inactive-client calls.

The established inherited round-search source omission is closed. The static
latch deliberately retains reference lifetime; source acceptance is not loaded
QC/gameplay/restart/load qualification. All executable checks remain deferred
until implementation ends. No whole QC inventory or final-goal acceptance is
inferred from this one-file adapter.
