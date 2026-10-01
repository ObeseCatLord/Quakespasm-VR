# C01/C03/C05 native networking correction plan

2026-10-01. Before-code plan against the final senior-reviewed checklist.
Source inspection only; all executable qualification waits for full implementation.

## Behavior and evidence

C01: a late QC model precache must populate the client model cache rather than
sound slots. Current Quake/pr_cmds.c PF_sv_precache_model duplicates the native
SV_Precache_Model loop but writes tag 0x8000. The existing helper writes the
correct model tag 0x0000 and owns model loading/publication. QSS-M03a498 uses that
same thin wrapper/helper arrangement. Preserve QC's returned original string,
empty-string check and overflow error. Normalize late-warning policy to the
existing !pr_checkextension gate, for cached and new entries, without another
model lookup. Do not import QSS-M's precacheanytime state or change capabilities.

C03: ordinary world/player SSQC infokey requests must read the existing serverinfo
and active-client userinfo stores. Current Quake/pr_ext.c returns NULL in those
two fallback branches. Reuse QSS-M's Info_GetKey(buffer,sizeof buffer) fallback;
empty values become NULL for the current zero/empty return conventions. Preserve
all special keys, active-player checks, temporary string lifetime and conversions.

C05: the challenge reply currently advertises DP7 and NEHAHRABJP3 although the
client server-header parser accepts NQ/Fitz/RMQ only. Remove those two offer
tokens in Quake/net_dgrm.c. Keep the distinct darkplaces 3 control handshake and
RMQ FITZ QUAKE tokens; do not add a dialect, socket or negotiation state machine.

## Smallest design and ownership

Thin wrappers and two literal fallbacks reuse the native model, metadata and
transport owners. A separate precache protocol/registry, new metadata store or
new transport would duplicate existing state without evidence of incompatibility.
Exact production write set: Quake/pr_cmds.c, Quake/pr_ext.c, Quake/net_dgrm.c.
One Luna xhigh worker implements this slice; main owns review/integration and docs.
Other workers use disjoint audio/renderer files. Keep the patch to these seams.

## Stages and eventual acceptance

1. Replace the duplicated model loop with the native helper and explicit warning
   gate; preserve the existing builtin binding and QC return/error behavior.
2. Add the two QSS-M metadata fallback blocks without disturbing special keys.
3. Trim unsupported challenge offers only.

Final software qualification must load a QC-triggered late model with a remote
client, preserve sound slots, exercise existing/empty/overflow cases, and query
custom world/player metadata plus absent/inactive/special keys. Capture supported
challenge negotiation and ensure native NQ/Fitz/RMQ connection behavior remains.
C02 metadata publication remains a separate prerequisite for joining-client
metadata acceptance; restoring lookup alone does not close C02. No tests/builds,
probes, fixtures or game runs during this implementation slice.

The final scope Astra review already identified/adopted these corrections. There
is no new architectural fork; reopen if implementation requires state, protocol,
capability changes or broader file ownership.
