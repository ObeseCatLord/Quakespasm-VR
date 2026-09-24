# QBJ3 paired viewmodel client: senior design review

This review concerns the first client vertical for QBJ3's split twin nailgun on
the vkQuake-based `2.0` branch. The server hook is already gated to the exact
installed QBJ3 program, and its native QC fixture passes two physical shots.
The client must show both halves and send the same physical muzzle poses before
the server advertises the private capability. Headset fit is deferred to the
user's live testing.

## Reference and incompatibilities

The inherited VR implementation in `vr.c` selects a complete pair, draws both
halves with source weapon animation, and derives muzzles from their held model
transforms. Its source contact anchors are left
`(54.75913167, 10.28241703, -16.05048694)` and right
`(54.75913167, -10.49037877, -16.05048694)`. The vkQuake loader already
generates the private left and right MDLs from the exact source model
(`Quake/gl_model.c:680-835`). The target alias renderer already owns viewmodel
held offset, scale, mirroring, lighting, and culling (`Quake/r_alias.c:86-89,
651-731, 846-1010`). Its input path already transforms raw MDL contacts with
`R_AliasModelMatrix` (`Quake/vr_input.c:1715-1743`). These are reusable seams.

The donor mutates alias headers and draws through an older OpenGL path. The
target uses Vulkan alias draws, may select enhanced MD3/MD5 source geometry,
and can draw the viewmodel on a render task. Those are demonstrated boundary
differences, not reasons to replace the loader or renderer.

## Decision and Astra disposition

| Review point | Disposition |
| --- | --- |
| One owner for draw and muzzle math | Accept. Extend `R_AliasModelMatrix` for explicitly identified physical halves; use it with zero-origin command-space lerp data for muzzle anchors. Reflect the left grip residual, never the split mesh. |
| Main-thread loading | Accept. Prepare/validate the pair and immutable presentation poses after the stereo `V_SetupFrame()` on the main owner in `gl_screen.c`. Render tasks only consume prepared entities. Desktop `V_SetupFrame()` can run on a worker. |
| Classic model provenance | Accept. Use loaded `source->extradata[PV_QUAKE1]` for exact source frame/scale checks; leave enhanced selection unchanged for desktop and ordinary fallback. |
| Atomic pair admission | Accept. Require exact selected/view model identity, frame agreement, server capability, both accepted and tracked hands, and two valid matrices. Drop both halves and both wire poses together on failure. Recheck pending identity at command send. |
| Existing vkQuake graphics | Accept. Draw through the existing alias path and extend its viewmodel recognition for paired lighting, movement interpolation, culling and debug passes. No separate pair pipeline. |
| Loader recipe mapping | Accept. Expose a small accessor to the existing table. Gameplay eligibility and contacts stay in the client. |
| Full donor subsystem for all four recipes | Defer. Only QBJ3 has matching server semantics now. Porting all would introduce extra state and untested protocol behavior. |

The smallest end-to-end proof is: load both generated halves from the installed
source, display them at separate tracked hands with source animation, emit two
physical muzzle/aim poses from the same matrices, run a real private command
through the pinned server and installed QC, then confirm ordinary desktop and
one-hand/unavailable-capability fallback. Packet counters or matrix unit checks
alone are insufficient. The server capability remains off until this proof is
implemented and software-tested. Physical headset alignment is the user's later
test.
