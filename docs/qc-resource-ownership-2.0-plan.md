# QC file and string-buffer ownership

## Verified behavioral reference

Primary `Quake/pr_cmds.c:5120` (`PF_GetQCFile`) and 5521 (`PF_GetStrBuf`)
require the resource's owner to equal the current VM before accessing it.
Destination `Quake/pr_ext.c` already stores `owningvm` in both native tables
and cleans only the current VM's resources at shutdown. However, the older
file calls (`fgets`, `fputs`, `fclose`, `fseek`) check only that a file is open;
older buffer calls check only that an owner exists. A CSQC program can thus
use an SSQC handle to read, overwrite or free the other VM's resource. New
buffer-file and search adapters already enforce current-VM ownership.

Destination also compacts holes in `PF_buf_sort` without clearing the old tail.
For `[A, null, B]`, compaction leaves `[A, B, B]` with used count two. An append
at the new end frees the stale tail pointer, leaving live B dangling. Primary
`PF_buf_sort` preserves the old count and clears the tail after compaction.
This is a demonstrated lifetime incompatibility at the existing buffer owner.

## Minimal adapter

Keep native dynamic file allocation, file cache/pack offsets/modes, buffer
allocation/content operations, `Mem_*` ownership and shutdown functions.
Change the existing entry checks to require `owningvm == qcvm`, copying the
primary access rule without replacing native resource tables or operations.
Both source and destination buffers must pass that check for copy operations.

Use one private, stateless index decoder for existing numeric handles. It
rejects nonfinite, below-base and beyond-table values before float-to-integer
conversion and returns the table limit as the invalid sentinel. Existing
caller bounds checks then reject it before any table indexing. Finite positive
fractional handles retain the existing truncation behavior. Keep this helper
limited to handles; file modes, seek offset representations, buffer string
indices, formatting and content truncation remain their existing contracts.
Do not introduce generations or claim stale same-VM handles cannot alias a
subsequently reused slot; primary has the same numeric reuse behavior.

Reuse the decoder in the newer buffer-file wrappers too, removing their
duplicated handle conversion while keeping mode, filename, owner and bounded
start/count checks. Native file diagnostics and existing invalid-operation
returns remain; initialize invalid `buf_getsize` to float zero and
`buf_implode` to string zero as primary does, instead of exposing a stale
return value when the new owner rejection is taken.

Copy only primary's sort tail clearing into native compaction, using the old
used count. Native sorting order/prefix rules remain unchanged. No whole buffer
rewrite, new resource manager or public API is justified: the existing owner
fields, shutdown paths and operations are reusable.

## Scope and acceptance

Production writes: `Quake/pr_ext.c` only on `2.0`. This plan precedes code.
Bounded local Astra source review must trace all public native file/buffer
handle consumers, both operands for copy, finite/bounds-before-conversion,
current-VM checks, unchanged mode/cache/offset semantics, newer file wrappers,
cleanup and compaction ownership. No renderer, network or VM-dispatch change.

Builds/tests/compiler/runtime probes remain deferred until implementation is
finished. Final Linux/ARM checks must use SSQC plus CSQC resources concurrently:
owned operations succeed; foreign/closed/invalid handles cannot read, mutate
or free them; one VM shutdown preserves the other's resources. Exercise pack
reads, write/append/seek, numeric extremes, copy source/destination permutations,
empty/invalid query returns, buffer-file wrappers and sparse sort followed by
append/set/free/delete. Repeat across map/client-VM lifetimes. This is source
adaptation, not software/runtime acceptance.
