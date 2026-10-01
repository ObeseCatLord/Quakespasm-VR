# Native entity-search compatibility reconciliation

2026-09-30. Before-code plan and bounded source audit. Preserve vkQuake's VM,
registrations, traversal and error policy; reuse inherited compatibility at the
existing builtin boundary. No new entity registry or replacement search engine.
Tests/builds/probes remain deferred until all implementation is finished.

## Verified source and decision

Requested local Astra xhigh inspected current `pr_cmds.c:PF_Find` and
`pr_ext.c:PF_findfloat/PF_findflags/PF_findchain/PF_findchainfloat/PF_findchainflags`
against pinned primary `51b452c018273647dcf94f4628a370267ff8fa91`, QSS-M
`03a498aabc411e2e739adc815c5536b161b9626e` and vkQuake
`4bc898f29073e8aa41069f0e79e3cb5a9eb73afa`. Main spot-checked the load-bearing
primary predicates and the additional `PF_nextent` caller. Effective reviewer
metadata is unexposed: requested-Astra source advice, not certified signoff.

Native core and the five extension handlers already preserve iterative search
after start, ascending first match, descending prepended chains, optional chain
field, string contents, float equality, overlapping integer flag bits, world
sentinel and existing SSQC/CSQC registration. Keep them. Preserve native string
error behavior rather than importing primary's broader VM policy changes.

The inherited four-function fingerprint in primary `pr_cmds.c:1916` recognizes
programs with `centerprintlocal`, `teleport_check_for_client`,
`teleport_enter_limbo`, and `spawn_tpush`. Their allocated disconnected client
edicts can stop local centerprint iteration and occupy co-op teleport destinations.
Primary filters these slots in **both** `PF_Find` and `PF_nextent`; current native
loops only skip free edicts. This is inherited program compatibility, not a new
directory/mod-specific framework. Ordinary programs deliberately retain native
enumeration of disconnected bodies.

| Recommendation | Main disposition |
| --- | --- |
| Reuse primary's narrow inactive-client filter | Adopt. Copy fingerprint/cache and slot predicate into existing `pr_cmds.c`, add an explicit SSQC guard because native also permits CSQC calls. Apply to both existing core find and nextent loops. |
| Replace the five extension handlers | Reject: no demonstrated behavior omission; leave native code and registrations. |
| Import all adjacent debug/VM/contact policy | Reject: independent owners and error semantics must not be replaced incidentally. |
| Shub's Wager result/cleanup search suppression | Established separate omission. Plan its exact inherited dependencies separately before implementation; do not infer generic suppression or attach it to every extension search. |

Minimal change: approximately 40 helper/comment lines and two loop conditions
in `Quake/pr_cmds.c`, no other production files. Reuse primary's program pointer/
CRC cache; before any cached lookup or client-array access reject non-SSQC VMs.
Preserve world and non-client slots, active clients, return encoding, current
field/start handling and native lazy binding. No deployment or asset edits.

## Final software qualification, after implementation

Exercise actual loaded QC through core `find` and `nextent`: all four fingerprint
functions versus missing one; stale non-free player in an inactive reserved slot
before an active player; matching ordinary non-client entity; active/free/world
and exhausted search; program changes; ordinary mod disconnected-body semantics;
CSQC enumeration and all five unchanged extension searches. Include real co-op
centerprint/teleport destinations through existing clients. Helper outputs alone
are not a gameplay proof. Final Linux and isolated native ARM qualification are
required; live headset tests and Windows builds remain deferred.

## Source acceptance of inactive-client slice

Production commit `4e35dd69` copies the primary helper pair and two loop
conditions into native pr_cmds.c (46 additions, two deletions). Explicit SSQC
guards precede fingerprint/cache and client-array access. Main reviewed the
complete actual diff and scoped whitespace check; requested local Astra xhigh
then compared the committed helpers and **both** actual callers against donor.
No introduced P1/P2 was found in this bounded source pass. Native CSQC, all five
extension searches, registrations and error policy remain unchanged.

This closes the established inactive-client source omission only. Actual QC
reload, co-op destinations/centerprint, all other search contracts and final
Linux/ARM software qualification are pending. The independent inherited Shub
result/cleanup suppression omission still needs its own narrow implementation
plan; do not mark the whole QC inventory or goal accepted from this patch.
