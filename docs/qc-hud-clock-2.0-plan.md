# CSQC HUD intermission clock adapter

2026-09-30; before code on2.0. Main source comparison found that primary master
51b452c0 sbar.c sets optional intermission_time to cl.completed_time before
CSQC_DrawHud, as well as before CSQC_DrawScores during intermission. Destination
Sbar_DrawCSCQ already seeds frametime, cltime, clframetime, intermission,
player_localentnum and time before the HUD callback, but omits intermission_time.
Its native Sbar_IntermissionOverlay already supplies that optional clock to the
score callback, and progs.h already registers/binds the global.

Copy the primary two-line optional-global assignment into Sbar_DrawCSCQ beside
its existing intermission assignment. Both callbacks then receive the same
client-owned completed_time value. Retain actual callback timing, GUI/QC mutex,
VM switching, native stats restoration, screen-error recovery, viewport/display
extent, desktop/VR panels and the existing intermission overlay. No new clock,
HUD callback, VM field, protocol or cache is needed. Write only Quake/sbar.c.

Replacing the HUD path or adding a second frame-state publisher duplicates the
existing owners for a missing optional assignment; reject that replacement.
Expected production delta: two copied lines. Main integrates; a bounded local
Astra source review checks reference mapping, existing global binding and both
callback contexts. No builds/tests/compiler/runtime probes or fixture changes
until full implementation. Final Linux/ARM checks cover a declared/absent global,
ordinary HUD and intermission score callbacks, coherent completed_time, map/reset
and callback-error handling, with both desktop and the existing VR panel path.

The accompanying MOD-003 comparison confirms existing native source behavior:
drawsubpic accepts source position plus size and maps through the cached image's
UV rectangle, as primary does. Primary GL_SetCanvasColor selects blend below
alpha1 and alpha-test at alpha1; native Draw_SubPicInternal makes the same choice.
Retain native picture filtering/shaders and successful wrapper argument layout.
This is source reuse evidence, not rendered alpha/edge/padding qualification.

## Implementation and source disposition

Production666cdab1 copies the planned two lines. Local requested-Astra/Max source
advisory found no actionable P1/P2 in the exact delta: completed_time matches
primary and native intermission-score context; the float global is already bound
by the native registration macro through PR_FindExtGlobal, with NULL for absent
or incompatible declarations; assignment occurs after client VM selection and
before the existing HUD/score callbacks. Scheduling, mutexes, canvas, parameters,
VM/stats restoration and error handling are unchanged by the patch.

Main independently inspected that binding and both callback contexts. Scoped
git diff --check passes. Effective reviewer settings were not exposed, so this
is bounded source acceptance without settings certification. No builds, tests,
compiler/runtime probes or fixtures ran; broader GUI/stereo/runtime acceptance
remains deferred. The existing picture path remains reused without code changes.
