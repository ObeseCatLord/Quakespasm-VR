# F03 loaded field reflection and relinking plan

2026-10-01. Before-code plan. Existing source repairs in0720c36a retain native
VM/hash/parser/zone/spatial owners; [earlier design](qc-reflection-2.0-plan.md).
Main reread native pr_edict.c1301/1892/2263 and pr_ext.c4324–4390 against inherited
pr_cmds.c5974–6030. No additional production defect established; no rewrite.

Native field-map insertion is reversed to preserve the first duplicate definition.
When native extra fields are missing, merge copies the table, rebinds each actual
hash winner and appends fields/components; component internal SAVEGLOBAL flags
are masked at the reflected API. When all eight engine fields already exist,
the original loaded table stays in place. Field parsing reuses ED_ParseEpair;
valid SSQC writes relink without trigger touch, including failed function parse.
CSQC/invalid index writes do not relink. Native zoned writes copy before retiring
the prior zone, including self/interior aliases. Preserve all these owners.

Minimal proof: existing generator gains separate reflection mode and optional
complete-engine-fields input variant. Append two same-name float fields at
distinct offsets, then optionally supply native alpha/scale/emiteffectnum/
traileffectnum/tag_entity/tag_index/modelflags floats and colormod vector with
plain float components. Retain all original field/other section prefixes,
version/CRC; entity width intentionally grows by declared fields. This is test
metadata, not a new compiler/registry. Existing six modes remain byte-identical.

Actual QC entries query name/index/count/type/raw field offset, read/write entity
fields via native handlers, and provide a simple actual trigger callback. Native
C supplies inputs only, allocates real edicts and checks interpreter results.
Enumerate loaded fields using independent first-name traversal as expected
winner; observe merged versus original-table pointers and component masks.
Query unknown/out-of-range names/indices with native nonzero return priming.

Both VMs parse scalar/vector/string/function fields. Cover zoned literal,
self-assignment, interior alias, empty/repeated replacement and current ownership
bits, never reading retired pointers. Real SSQC area-list membership/abs bounds
must update after reflected origin/mins/maxs/solid changes. Failed think parse
returns0 but still relinks; invalid-index write refuses without relink. CSQC uses
its own real native area tree built from the loaded BSP and remains unlinked
after reflection, preserving the SSQC entity/list/program. No copied/fake nodes.

A real overlapping trigger with appended QC touch changes the moving entity's
health only under a later explicit native touch-positive-control. Reflected
writes retain health/touch count, proving no-touch flag with a usable trigger.
Use no public clients, rendering, devices, microphones or sockets. Reload each
VM/variant through normal loader, compare field metadata and independent owners.
Exact lifecycle claims stay bounded; no freed-handle generation guarantee.

One Luna/xhigh coding worker owns only generator, <=180 added Python lines;
main owns existing native fixture/private profiles/docs, <=230 added C lines.
Contracted global/function names fixed before code; no overlapping edits. Worker
is not alone, may not delegate/run/build/edit production/docs/branch/commit;
stop/report missing evidence or expansion. Main reviews source and executes
strict dedicated CPU checks only after slice implementation, then bounded local
Astra evidence/claim review. No second VM/parser/allocator/area system.
GPU remains stopped. No new feature inventory, Windows/ARM or whole F03 closure;
entity searches/surfaces/authored consumers/GUI unwind remain separate boundaries.

Before execution, main found PR_LoadProgs intentionally does not allocate edicts;
that belongs to the native server/client bootstrap. Refine the proof to invoke
actual CL_LoadCSProgs through a7line test-only host-owner shim, replacing only
host object in this fixture link, and native SV_SpawnServer for server reload.
Reflection program admits native client loading with a real no-op Ent_Update
declaration. Use normal csprogs.dat filename/real loaded BSP; do not duplicate
allocation/init or manually set callback pointers. This is a narrow test boundary
adapter, not production API/loader policy. All other host code remains native.

Loaded execution exposed one additional production defect before implementation:
merged colormod_x lookup returned0 rather than loaded index234. The native hash
map copies pointer-sized keys, not string bytes, while the component merge inserts
a va() buffer pointer. Later native formatting reuses that storage. Preserve the
same merge/hash/string owners: after ED_NewString, obtain the component name from
its registered VM string for hash insertion. No copied-key map, new allocation,
linear lookup or adjacent loader change. Add this single-line key-lifetime repair
in pr_edict.c, rebuild that native object and rerun both field-table variants.
Retain the failing receipt and ask the bounded Astra review to verify lifetime.
