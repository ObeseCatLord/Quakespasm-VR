# C21: truthful native GPU diagnostic availability

2026-10-01. Before-code plan; executable checks wait until implementation ends.

## Verified behavior and seam

Keep vkQuake's existing query pool, fence completion, per-slot query ownership,
CPU/wait counters and `scr_speeds` presentation. The inherited native diagnostics
are the behavioral reference, with the migration's existing VR AO extension.
`GL_TimestampElapsedUs` already rejects invalid widths, wrap and invalid elapsed
values. However `GL_BeginRenderingTask` resets frame GPU time to zero and ignores
the helper's return, so unsupported/not-ready/failed/wrapped queries appear as
measured zero. AO has a validity bit but disappears entirely when unavailable.
Two-view GPU work is shared; it must not be described as independent eye times.

## Minimal adapter versus replacement

Add frame-sample validity alongside the existing AO validity. Carry the sampled
slot's stereo mode alongside each reported sample, because current mode can
change before an old slot is read. Do not divide GPU elapsed time by two or add
queries. Clear validity/mode when publishing no sample; set validity only after
a successful native query read and elapsed conversion. A true measured zero is
valid. Keep all native timestamp cadence, reset, fence and submission semantics.

In `R_PrintStats`, retain the native desktop format for valid samples. Show
`n/a` for unavailable frame GPU samples. Label valid stereo work as shared GPU
work, using the sampled mode, not the current frame. At quality diagnostic mode
3, retain one AO line and show a valid desktop/stereo AO interval or `n/a` /
disabled status. Respect the existing four 40-byte lines; do not expand the HUD
canvas or fabricate per-eye values. AO execution/quality and desktop algorithm
remain unchanged.

## Ownership and acceptance

Write set: `Quake/gl_vidsdl.c`, `Quake/gl_rmain.c`, `Quake/glquake.h`; around
40–80 lines. Only observational sample metadata and formatting change.
Consolidated final checks cover timing disabled/reenabled, unsupported queues,
first-slot/no-AO, failed/not-ready/wrapped queries, valid zero and desktop/VR mode
changes. Verify that stereo labels match the captured slot and the output fits
the native diagnostics. No benchmark/performance measurement is required.
