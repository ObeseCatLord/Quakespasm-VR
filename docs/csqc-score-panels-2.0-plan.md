# CSQC score, death and intermission panels

## Goal and verified source boundary

Preserve the inherited VR HUD anchor and native vkQuake desktop drawing while
bringing CSQC-owned gameplay scores/death and intermission onto tracked panels.
No new scoreboard, QC dispatch, offscreen target or OpenXR composition layer.
This is a solo-maintainer adapter, not a parallel UI architecture.

| Source fact | Evidence checked by main |
| --- | --- |
| Primary gameplay HUD and scores share one `VR_DrawSbar` anchor. | Readonly primary `51b452c0:vr.c:VR_DrawSbar`; `sbar.c:Sbar_Draw` invokes HUD then optional scores together, including scores-only native branch. |
| Primary intermission uses its head/menu panel and one score callback. | Primary `vr.c:VR_Draw2D`; `sbar.c:Sbar_IntermissionOverlay` supplies the panel's 320x200 display, separately from the AD-wide gameplay HUD. |
| Current gameplay explicitly excludes CSQC score/death. | `gl_screen.c:SCR_VRClassicSbarFrameEligible`; current `SCR_VRClassicSbarPrepare` already prepares a 320x200 or inherited AD-wide CSQC display and inverse viewport transform for ordinary HUD. |
| Current intermission explicitly excludes CSQC scores. | `SCR_VRNativeIntermission`; `Sbar_IntermissionOverlay` retains native CSQC score dispatch. |
| Native fallback/inventory can draw alongside CSQC. | `sbar.c:Sbar_DrawClassic` draws inventory before scores-only QC; `Sbar_DrawCSCQ` can fall back to native deathmatch scores on `CANVAS_MENU`. |
| Native mixed canvases need an explicit unit conversion. | `gl_draw.c:GL_SetCanvas` scales `CANVAS_SBAR/MENU` by desktop cvars, while CSQC uses its override pixel scales. Within the inverse CSQC transform these differ; removing eligibility checks alone cannot preserve physical artwork size. |
| An existing local override and clip/error owner can be reused. | `SCR_GetCSQCDisplay/SCR_SetCSQCDisplayOverride/SCR_CSQCDisplayOverrideActive`; `GL_SetCanvas` clears source clips on leaving CSQC; `SCR_DrawGUI` clears overrides, ends the panel, releases its QC mutex and tears down failed QC on longjmp. |

Source presence is not runtime qualification. No tests/builds/probes run here.

## Proposed minimal adapter, before coding

1. Extract the existing CSQC display/inverse-viewport transform into one small
   `gl_screen.c` helper. Parameters are explicit canvas bounds, physical scale,
   prepared anchor/basis and explicit source pivot. The HUD
   call must produce the existing y=0-anchored transform unchanged; the
   intermission call uses canonical 320x200 centered bounds at `vr_menu_scale`.
   Retain the inherited AD-wide gameplay predicate; add no new mod cases.
2. Admit gameplay HUD+scores and scores-only score/death states through the
   existing classic preparation. Use CSQC bounds whenever the actual CSQC
   presentation is selected; retain reentrant VM guards and once-frame owners.
   Keep exactly one native `Sbar_Draw` call and its HUD/scores ordering.
3. Reuse active panel plus existing CSQC display-override presence to convert
   native `CANVAS_SBAR` and `CANVAS_MENU` on that panel. Pixel scales from the
   override map each native source unit to the same physical size as QC glyphs.
   Center the native 320x48 strip horizontally, with its source y=0 at the
   unchanged HUD target; retain the native VR 416x200
   score surface starting at source y=-152 and ending at y=48. Apply only
   in that scoped override, so desktop, modern HUD, menu and console stay native.
   Keep the existing classic-layout flag true for CSQC physical HUDs too,
   reusing inventory, voice and mini-score policy without editing their draw
   owners. No independent voice HUD or new context-state flag is needed.
4. Admit CSQC intermission through the existing intermission mode. Prepare its
   display and CSQC transform alongside the existing native menu transform,
   from the same immutable anchor. Those are two canvas transforms, not two
   tracking poses or draw passes. Begin the selected transform, scope the
   display override around the one native `Sbar_IntermissionOverlay`, then clear
   it/end panel. Keep modal/loading precedence and native finale behavior.
5. On QC failure reuse the existing cleanup. A recovered intermission must
   choose the already-prepared native transform and cleared override, rather
   than reuse the failed CSQC transform for native output. Existing gameplay
   failure invalidation and flat native fallback remain. Check success and
   longjmp lifetimes, including source clips and canvas cache invalidation.

### Alternatives and open review decisions

| Option | Proposed disposition |
| --- | --- |
| Remove score/death eligibility checks only. | Reject: mixed inventory/native fallbacks retain mismatched desktop canvas scales. |
| Duplicate the scoreboard/HUD draw or invoke QC once per eye. | Reject: duplicates stateful QC and draw ownership. |
| Add an offscreen target/composition layer. | Reject: existing Vulkan panel, clipping and display-override owners can handle the demonstrated boundary. |
| Add another panel-context flag/display state. | Avoid: existing active-panel plus thread-local override identifies this narrow conversion. Review must challenge whether that predicate has unrelated consumers. |
| Keep one intermission matrix and rebuild during error recovery. | Prefer two precomputed canvas transforms from one anchor: task error handling should not advance tracking or mutate main-owned preparation. Review can simplify if an equally correct existing conversion removes this need. |

Gameplay and intermission are two placements using the same display/transform
math, not separate UI implementations. Review may merge/delete duplicated
work. Verify the claimed overlap and native canvas conversion before critiquing.

## Ownership, bound and failure path

Production scope after review: `Quake/gl_screen.c`, `Quake/gl_draw.c` only,
at most 300 net lines. Reuse current `csqc_display_t`, native render-task/VM
mutex, panel begin/end and draw owners. No shaders, renderer state machine,
new protocol, input subsystem, VM registry, mod policy, avatar or demo work.
Reopen the design before coding beyond this bound or adding a new owner.

Requested local Astra source advice must inspect actual producers/consumers and
challenge clipping, native fallback, display bounds, errors, arbitrary callback
behavior and desktop isolation. Effective model/effort metadata is unavailable,
so do not label advice a formal skill certification. Main records dispositions
and delegates bounded coding locally; no web models are authorized for now.

## Final acceptance after all implementation

Exercise real registered QC HUD+scores, scores-only, HUD-only/native deathmatch
fallback, explicit scores, death, solo/co-op/deathmatch intermission and finale.
Verify one callback execution per native frame/expected dispatch, inherited
placement, stable physical glyph size at varying render/cvar scales, AD-wide
gameplay bounds, clipping/reset and native voice/inventory alignment. Inject
recoverable errors in gameplay/intermission and verify cleared override/clip,
released mutex and native fallback transform. Preserve unchanged desktop,
modern/native HUD, menu/modal/loading and non-VR-runtime operation. Include
current Linux and Linux ARM software paths at the final consolidated gate.
Live headset, roster readability and performance measurements remain the user's
later checks. No claims of arbitrary roster fit or runtime parity from source.

## Adopted local Astra source dispositions, before coding

Main checked the load-bearing source findings against native viewport/panel
composition, `Sbar_DrawClassic` inventory admission, and primary's VR canvas
bypass/HUD transform. The requested review is source advice, not model/runtime
certification.

| Finding | Disposition |
| --- | --- |
| The proposed bottom alignment would move native inventory when CSQC scores activate. | Adopt correction: shared transform uses explicit source pivot `(W/2,0)` for unchanged HUD and `(160,100)` for centered intermission. Native SBAR maps source y=0; native MENU maps y=-152. No bottom-at-200 policy. |
| Scores-only loses forced VR inventory when the classic-layout flag is disabled. | Adopt: set the existing classic-layout flag for every classic physical HUD, including CSQC; keep native inventory/voice/mini-score consumers unchanged. Override conversion takes precedence for the two affected canvases. This removes the proposed sbar.c edit. |
| Intermission error recovery cannot reuse the CSQC matrix after clearing the override. | Adopt: preserve native matrix and add one alternative from the same anchor; choose again after longjmp using volatile recovery state. Native fallback uses its original matrix and no override. |
| One scalar canvas scale is insufficient, and centering all 416 shifts names. | Adopt: use independent override pixel scales; preserve alignment of the first 320 units. Native HUD converter requires active panel, classic-layout flag and existing override, limiting it to the gameplay placement. Intermission QC uses its own canonical full-viewport canvas. |
| Override changes do not invalidate cached canvas; arbitrary QC callback ordering must stay native. | Adopt: install override before first canvas selection after panel begin, clear/end on success and existing error cleanup. Preserve native HUD→scores call/clip semantics rather than adding resets or transactional draw rollback. |

For native HUD conversion, framebuffer viewport x is `(glwidth-320*px)/2`;
native canvas height is 48 (SBAR) or 200 (MENU), source y is 0 or -152,
and GL_Viewport's lower-origin y argument is
`glheight-(source_y+canvas_height)*py`. This explicitly inverts the CSQC
full-viewport mapping and keeps each native pixel at the HUD's physical scale.
The current wrapper's classic flag already owns the required inventory, voice
and mini-score behavior; no second consumer predicate or new context flag is
needed. Keep callback dispatch distinct for HUD+scores, HUD-only native
deathmatch fallback, and scores-only explicit scores/death.

## Source integration and final advisory

The adapter is implemented in the two planned files:114 added and47 removed
production lines,67 net. Main reviewed the complete diff against the native
canvas/panel composition and actual sbar dispatch. Gameplay retains its y=0
source anchor; the shared helper centers intermission separately. Mixed native
inventory, scores and voice reuse the classic-layout flag and independent
override pixel scales. The native intermission matrix remains available after
QC failure, selected using the post-longjmp volatile recovery flag.

The requested local Astra final read-only source advisory found no P1/P2 blocker
in this scope. Main accepted its transform, eligibility, inventory/voice,
override/cache/clip and recovery findings against the actual diff. Scoped
whitespace checking passed. No builds/tests/probes were run. Already-recorded
commands before a QC failure are not rolled back; that native limitation remains.
This is source integration, not runtime or model certification. The final
software acceptance matrix above remains pending after all implementation.
