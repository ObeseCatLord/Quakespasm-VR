# VR crosshair and startup defaults follow-up

Before-code plan, 2026-10-04; production `2.0`, baseline `ca002987`.
Write ownership: `Quake/gl_rmain.c`, `Quake/gl_screen.c`, crosshair-only
`Quake/vr_input.c`, `Quake/cl_input.c`, and this document. Menu/draw, main,
configs, shaders, build settings and the legacy checkout are read-only.

## Verified evidence

- `Misc/vq_pak/default.cfg:109` explicitly assigns `viewsize 110`, overriding
  `gl_screen.c`'s C default of 100. `SCR_CalcRefdef` clears `sb_lines` when a
  classic VR panel owns the HUD, but never changes `viewsize`. Native
  `Sbar_DrawClassic` forces the inventory for its VR panel; CSQC receives the
  actual cvar and can select its compact HUD at 110. `sizedown` changes 110 to
  100. HUD capture therefore does not itself supply the requested default.
- `cl_input.c` already defaults `cl_alwaysrun` to 1; packaged `default.cfg`
  does not override it. Both desktop and VR movement use the existing speed
  XOR policy. Saved 0 remains an intentional setting, not a migration target.
- `R_PrepareVRCrosshair` snapshots calibrated one/two-hand aim and traces on
  the main owner before scene tasks. Retain those producers and gates.
- `R_DrawViewModel` draws the crosshair before the weapon. `r_passes.c` also
  executes this context before transparency/OIT resolution and blended
  particles. The crosshair writes no depth, so subsequent geometry can cover
  it. The existing `SCBX_WHEEL_FOREGROUND` stereo scene context executes after
  these stages and already hosts late weapons; no new pass is necessary.
- `R_SetupMatrices` uses a 90-degree center projection on both axes in stereo.
  The crosshair instead computes its pixel extent with desktop-derived
  `r_fovx/r_fovy`. This makes dimensions depend on mirror aspect/FOV and can
  reduce one axis. Use the actual center projection's 90 degrees.
- `R_CreateBasicPipelines` inherits disabled depth test/write and ordinary
  source-alpha blending. `R_CreateGraphicsPipeline` substitutes
  `basic_stereo_vert`; `R_BindPipeline` binds set 5; `stereo.inc` applies
  per-eye clip correction with `gl_ViewIndex`. These are working shared
  boundaries, not evidence of a driver or missing stereo-shader defect.
- Legacy `VR_ShowCrosshair` uses red point/line primitives, alpha .25, size 3
  with an 8-pixel floor, disables depth testing, and runs before the weapon.
  Preserve cvars, aim, physical depth and trace semantics. The scheduling
  repair deliberately prevents later scene geometry covering the pointer.

## Minimal adapter decision before implementation

Move the crosshair call out of the weapon function into the end of the existing
stereo foreground context. Keep existing visibility gates and immutable rays;
explicitly disable inherited fog constants when binding the basic blend
pipeline. Match point/ribbon extents to the fixed stereo center projection.
Estimated change: approximately 15 renderer lines, no renderer/state-machine
replacement, shader edit, pipeline creation, or ray fallback.

Add `vr_screen_defaults` in the existing screen owner: on an explicit OpenXR
startup request (unless `-novr`) or active stereo, assign viewsize 100 and
Always Run 1. This command belongs in `default.cfg` after native cvar defaults,
before saved config/autoexec/command-line/postcfg overrides. Expected helper
and registration: approximately 15 lines. Defaults are applied only by that
explicit config boundary, never every frame or on tracking/session changes.

**Required main integration, outside this slice:** append
`vr_screen_defaults` to `Misc/vq_pak/default.cfg` after its cvar assignments.
The existing native `quake.rc` startup and menu reset execute that file.
This slice must not edit it or pretend the unconnected helper already changes
fresh-profile startup. Preserve custom mod startup scripts; scripts replacing
the packaged default config must opt into this command to adopt its defaults.

A value-based first-VR migration cannot distinguish saved intentional 110
from a former default. A callback cannot observe same-value assignments, and
the command owner does not expose executing-config provenance. Reject guessed
migrations, config-text parsing, periodic overrides, or a second profile store.
The explicit default-script hook is smaller and preserves native ordering.

## Integration proof and limits

Hold builds, runtime/headset tests, commits and deployment for main integration.
Prepare private fixtures under `/tmp` and perform source/diff review only here.
After the hook is integrated, qualify fresh OpenXR profile (full native/CSQC
HUD, Always Run on), desktop startup, saved viewsize 110/120 and Always Run 0,
autoexec/command-line/postcfg same-value choices, and explicit defaults reset.

Crosshair qualification: both eyes; wall hit and fixed depth; point/line;
single/akimbo and noncontroller aim; mirror FOV/aspect changes; foreground and
playspace wheel; score overlay; standard/OIT/MBOIT; transparency, fog and
tracking loss. A valid ray should survive later scene geometry, while disable,
zero size/alpha, death/invisibility and invalid tracking retain their gates.
Hardware/runtime availability is unknown. Static findings establish concrete
ways to suppress/shrink the pointer, not the unique cause of the reported
headset failure or proof of physical visibility.

## Scoped implementation receipt

Implemented the plan in `gl_rmain.c` and `gl_screen.c`. The crosshair extent
helper simplifies `tan(90/2)` to 1; the obsolete forward declaration is
removed. The final scene draw explicitly clears fog constants. No crosshair
ray/input or movement edits were needed: the existing owners remain reusable.
Command registration happens in `SCR_Init`; execution through default.cfg
occurs after client initialization has registered `cl_alwaysrun`.

Scoped `git diff --check` passed. The private main hook patch also passed
`git apply --check` without applying it. Private artifacts are in
`/tmp/qsvr-crosshair-defaults-followup/`: `fixture.c` extracts the exact two
production helpers and covers center-projection pixel extents, invalid inputs,
desktop/novr, active VR reset, and later intentional defaults overrides;
`README.txt` contains deferred compiler/run instructions;
`main-default-cfg-hook.patch` adds only the required command to default.cfg.
The fixture is prepared, not compiled or executed. This is source acceptance,
not native config execution, Vulkan validation, or headset qualification.

The fresh-profile default remains **pending main's default.cfg hook**. Runtime
`vr_enable 1` without `-openxr` deliberately does not migrate an existing
desktop preference; an explicit VR defaults reset can invoke the command.
No saved-value heuristics, archived migration marker, or repeated override
was added. Main must qualify its selected mods' actual quake.rc ordering and
packaged default asset, then run the integration matrix above.
