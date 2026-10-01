# F01 early scoreboard publication repair

2026-10-01. The first actual connected desktop/VR trial fails before signon.
Diagnostic replay captures signon0/maxclients0/protocol0 and a ten-byte reliable
packet containing svc_updatename, slot0, Desktop. This is a valid slot delivered
before serverinfo allocated the scoreboard, not packet corruption. Native
+connect followed by +name produces the actual control command; retain that
ordering as a regression case rather than hide it in the harness.

Main inspected Host_Name_f, SV_UpdateInfo, SV_SendServerinfo and native/QSS-M
references. SV_UpdateInfo sends ordinary name/color fallback to every active
non-metadata/non-PREDINFO recipient, including unnegotiated clients. During pext
negotiation SV_SendServerinfo deliberately flushes already queued reliable bytes
before writing the header. The initial name can therefore arrive first. Donor
fallback also broadcasts to active recipients; provenance does not make the
observed current behavior acceptable.

Minimal adapter: require the existing recipient knowntoqc flag for ordinary
binary name/color fallback in SV_UpdateInfo. Reuse the same readiness boundary
already applied to native frag messages. Host_Spawn sends current known-player
names/colors/frags after serverinfo, and metadata peers keep their existing dirty
stores/phase drain. Name/color mutation and QC visibility still happen immediately.
Native server spawn resets knowntoqc on map changes. No new queue, negotiation
state, dropped-name store, client parser tolerance or forced signon is needed.

Compare alternatives: gating pe​xtknown would exclude legitimate no-extension
peers forever; client-only name deferral would not protect a joining client from
another player's updates. Reordering the test would remove the observed trigger
without repairing the native owner. A second publication state machine is larger
than the existing readiness boundary.

Bound BEFORE10 changed production lines, Quake/cl_main.c only. Add the native
readiness condition and a short explanatory comment; preserve all mutation,
metadata and successful live binary emission. Main reviews the entire patch,
rebuilds the affected host binary, repeats actual connected trial and native
metadata cases. Full Linux/ARM artifacts will need refresh after required
production fixes settle; accepted old artifacts remain evidence for their old
source, not qualification of this fix. This is a defect under F01/C02, not a
new feature or an expanded final list.
