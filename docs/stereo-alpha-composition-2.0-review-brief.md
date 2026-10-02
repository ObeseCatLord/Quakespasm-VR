# Native stereo alpha composition oracle: senior brief

2026-10-02. Solo engine migration; bounded existing F05 acceptance, no new feature.
Review a proposed output oracle, not production renderer architecture. Native
consumer observation and visible models now exist; no whole-goal signoff implied.

| Environment / evidence | Status |
| --- | --- |
| Writable 2.0 workspace | /home/obesecatlord/Documents/quakespasmvr/quakespasm-2.0; main reference read-only; no edits requested |
| Actual captures | /home/obesecatlord/FastGames/qsvr-alpha-colored-_ef3cp5j/output:16 B/E/W/C images, layers.json passed; natural engine/GDB exit0 |
| Runtime | Isolated Monado25.1 simulated null compositor; game Vulkan/native X11 mirror rendered; no real headset/gaze/benchmark |
| Binary | /home/obesecatlord/FastGames/qsvr-alpha-host-9x2fw7sl/vkquake-alpha: actual assert-enabled native common graph, cl_parse owner included via tests/stereo_alpha_native_fixture.c,2 private normal commands; shipping package unchanged |
| Pose/settings | alpha-layers.gdb under capture root: controlled +/-90roll, real water/empty eye leaves, masks1/2; MSAA4,SSAO1,OIT0, gamma/contrast1, palette0,waterwarp0, foveationoff |
| Inputs | two native Fitz static alias models wet(828,850,-297),dry(844,850,-295),12x1 planes, alpha128(.5) or1(zero); generated assets.json; native precache/parser/PVS/efrags/cull/list/draw reused |
| Scene frozen | cl.paused and cl.time exact across16captures asserted; changes only normal opacity/wateralpha/mirror commands |

Verified source facts:
- [verified source] Mod_FloodFillSkin gl_model.c:4168 consumes connected top-left
  background. Completely uniform initial skin becomes black. Corrected emitter
  uses top-left255/native skip and interior UV1..6; native texels[0][0]==255,
  fbtextures nonzero, gl_fullbrights1 asserted. Main inspected red/blue E image;
  first black-strip captures retained, not accepted as colored premise.
- [verified source/native] scene color format64=A2B10G10R10_UNORM, XR43=RGBA8SRGB,
  desktop44=BGRA8UNORM. Postprocess source decodes numeric scene RGB into linear
  for sRGB attachment, whose write encodes it again. Gamma/contrast1.
- [verified source] GL_CreateMirrorResources gl_vidsdl.c:4007 explicitly creates
  RGBA8UNORM snapshot for RGBA8SRGB XR; GL_RecordXRMirrorSnapshot:5630 copies
  raw color bits via vkCmdCopyImage; GL_SubmitXRMirror:4886 linearly blits
  UNORM snapshot to UNORM desktop. Thus main leans captured numeric RGB tracks
  the native scene/blend domain, no extra sRGB decode. Earlier direct SRGB-blit
  inference was rejected after reading snapshot creation.
- [verified official] Vulkan Copy Commands documents bit-compatible copies and
  source-sRGB decode for blits. https://docs.vulkan.org/spec/latest/chapters/copies.html
  https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBlitImage.html
- [verified source] native alias/world alpha blend SRC_ALPHA/ONE_MINUS_SRC_ALPHA,
  ADD, no alpha depth writes. Shader alias alpha=diffuse alpha*entityalpha,
  world alpha pushconstant; native opaque/SSAO graph preserved.
- [verified source] R_DrawAlphaEntitiesTask gl_rmain.c:2589: for exceptional opposite
  wet categories, same-category entities after water, other-category before water;
  per-eye scene descriptor/offset override; actual consumer seam pass in
  docs/stereo-liquid-consumer-current-2.0-results.md.
- [verified diagnostic only] Current16images each contain visible entity changes
  and water overlap, not acceptance yet. Using dE>20,dW>2 diagnostic pixels:
  ~4353..4677 overlap/eye, majority combined favors water-after-entity, minority
  entity-after-water; no classification/tolerance acceptance claimed.

Proposed oracle (mostly worked; open decisions):
1. Main lean: use measured B,E,W,C numeric RGB (0..255). For interior a=b=.5,
   entity-after-water predicted P_E=E+.5*(W-B); water-after-entity P_W=W+.5*(E-B).
   Reject empty/occluded/no-overlap; independently classify wet/red vs dry/blue
   through E-B signed color channel witnesses, then expected ordering from actual
   eye category. No fitted tolerance or using C to select candidate regions.
   Reject alternative: call/list counters alone cannot prove GPU composition.
2. Main lean: identify entity and water influence from B/E/W only, erode margins
   for MSAA/bilinear scaling/overlay/boundaries, require prediction separation
   comfortably larger than propagated quantization error. Need fixed justified
   quantization bound (10bit blend,8bit postprocess/mirror,X11readback) and adequate
   witnesses for both entitycategories/both eyes/reversed arrangements. Derive
   expected/wrong-order intervals, reject if wrong-order also admissible.
   Unknown: SSAO pass position can make four-layer equations unsuitable; verify.
   Reject alternative: guess3..10byte tolerance based on observed residuals.
3. Main lean: keep output checker/test-only profile small; no renderer readback
   layer or new engine commands. If mirror is insufficient, retain F05 open and
   propose smallest native acquired-eye readback adapter using existing ownership,
   rather than introducing Vulkan rewrite. This overlaps decisions1/2; merge freely.

Verify load-bearing claims in actual source and captures before critique.
Rank by leverage, pick one deep spec if worthwhile. Call out any invalid oracle
premise or native submission behavior, including quantization/filtering/clipping,
alpha effective coverage, material identity and mod/time/UI contamination.
No general renderer/VR rewrite, no live GPU run, no driver/system config, no
other F owners/whole-goal claims. Read-only ownership, no subagents. Return
<=1000words with exact file/line evidence and prioritized adopt/adapt/reject
recommendations; label unknowns. Human decisions only if actually necessary.
