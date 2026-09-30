# Existing palette-explosion QC argument contract

2026-09-30, plan before code;2.0 only. Keep vkQuake graphics, particle counts,
scripted effects, lights, sound and native temporary-entity transport.

The registered native `te_explosion2`427 signature is
`void(vector org, float color, float colorlength)`. Both handlers in
`pr_ext.c:2953/2967` currently read colorlength from OFS_PARM1, repeating color
instead of consuming OFS_PARM2. Native `cl_tent.c:267-274` independently reads
the two distinct palette bytes and selects its scripted/default effect with
both values. QSS-M has the same wrapper typo. Primary has the corresponding
native packet consumer/fallback, but no matching registered427 wrapper in the
checked `pr_cmds.c`; do not describe this as a missing primary registration.
These are direct source reads.

For example, a valid call with palette start0 and length8 currently passes
length0 to the native particle fallback, whose modulo requires a nonzero length.
Correct the two wrappers' argument selection only. The server still emits the
same two-byte palette record and the client still uses its existing named effect,
fallback, light and sound. No new effect renderer or parameter-policy service.
Do not import primary's1024 particles in place of vkQuake's512. Invalid palette
policy is outside this two-line contract correction, not claimed qualified.

Write set: exactly `PF_sv_te_explosion2` palcount and `PF_cl_te_explosion2`
colorLength in `Quake/pr_ext.c`, change OFS_PARM1 to OFS_PARM2. This is a routine
literal correction under existing native ownership; main may implement/review
directly without a design escalation or coding-agent overhead. The concurrent
read-only Astra file audit has a disjoint function scope and excludes this change.

No builds/tests/compiler/runtime probes until all implementation finishes.
Main checks actual diff, signature, distinct native decoder bytes and whitespace.
Final Linux/ARM software qualification covers differing start/length, start0
with positive length, server packet followed by ordinary opcode, both permitted
VMs, scripted naming and default particles. This does not certify all temporary
effects or actual desktop/two-eye rendering.

## Source integration checkpoint

`9ebfb93c` changes exactly the two wrappers' palette-length argument from
OFS_PARM1 to OFS_PARM2. Main inspected the actual diff, registered427 signature,
native decoder's two distinct palette bytes and retained particle/light/sound
consumers; scoped git diff --check passed. No particle count, transport, renderer
or invalid-input policy changed. No execution ran; final qualification remains
deferred as above.
