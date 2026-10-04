# Graphics toggle follow-up audit — 2026-10-04

**scope_done:** Final native GPU checks pass for desktop and both simulated OpenXR eye mirrors, with tasks enabled and both brush instance-buffer halves. Actual menu toggles affect actual pixels after the menu closes. No menu/SSAO effect-wiring bug was demonstrated. The authorized implementation clarifies the two help rows.

**Current-source proof:** Coordinated assertion-enabled debug binary: `/home/obesecatlord/FastGames/qsvr-upstream-bonk-final-debug-20261004/vkquake`. ELF Build ID `253143ebfd25cc2a1a87d77444ad3309736fe656`; SHA256 `a21c72dd2f47ade173fbe89d74bcf2ec949913caf947a139af09a0fef55505ad`. Its hash remained unchanged across all three final runs, and both new help strings are present in the executable. Current `r_brush.c:1238` and `:1283` use graphics instance base 1; compute still uses the half-buffer offset at `:3966`. Those changes belong to Locke/main, not this audit.

**Plan recorded before implementation:** Edit only `M_GraphicsHelp` in `Quake/menu.c`: explain active-effect lights versus baked map lighting, and contact AO near entity models. Preserve radius/strength and unsupported-device help, cvar wiring, defaults, SSAO, and shaders. Move the mistakenly created `newdocs/` audit into `docs/`. Prepare stationary-camera, task-enabled desktop/both-eye comparisons across halves 0/1; launch only after main coordinates the build.

**Changes:** Dynamic Lights now says “Lights from active effects; baked map lighting stays visible”. AO says “Contact occlusion near entity models; baked wall lighting stays visible”. Both fit the existing two-line help area. This agent changed only these help rows and this document. No defaults, rendering effects, shaders, builds, or commits were changed here. Source and documentation are ready for main's commit.

**Verification method:** Private licensed `start` profiles used paused geometry, fixed client time, average lightstyles, stationary camera, no viewmodel/HUD/notifications, 4× MSAA, GPU lightmaps, AO radius 32/strength 1, and full VR evaluation. Menu entry uses the native command queue; PID-targeted X11 Return/Tab/Right/Escape events exercise native menu handlers without changing focus. Every capture asserts closed menu/game input, actual cvars, and `R_RenderView(use_tasks)==1`. Dynamic Lights changes `r_dynamic` at `menu.c:2545`; AO cycles `r_ssao` 0–3 at `:2552`.

Desktop uses native GPU readback. XR captures the presented mirror of each actual eye image, tracking the brush half at successful snapshot recording and subsequent mirror presentation. Command-buffer slot is recorded separately. Neither half selection nor renderer scheduling is overridden. Monado runs privately with `simulated_hmd_create` selecting stationary movement; `SIMULATED_ROTATE=0` alone would still wobble.

**Pixel evidence:** 13 cases × two halves × two independent frames × three outputs = 156 final captures. RGB comparison region is `(0,60)-(641,481)` within 641×481 images. Counts below apply independently to each half; all repeated and cross-half comparisons are exactly identical within that region.

| Comparison | Desktop changed pixels | XR left | XR right |
|---|---:|---:|---:|
| AO off → low | 3,295 | 3,033 | 3,021 |
| AO off → medium | 2,792 | 3,055 | 3,099 |
| AO off → high | 2,928 | 3,146 | 3,237 |
| AO low → medium | 2,742 | 2,270 | 2,288 |
| AO medium → high | 2,150 | 2,247 | 2,422 |
| AO return to off | 0 | 0 | 0 |
| Native unlit → active light | 90,907 | 100,579 | 100,603 |
| Clustered unlit → active light | 90,289 | 99,979 | 99,975 |
| Dynamic off → matching unlit control, either mode | 0 | 0 | 0 |

Every AO off→quality comparison has negative mean RGB change; native active-light mean increases are 10.523117/11.823084/11.805436 channel levels. The probe supplies an ordinary `cl_dlights[0]` light 100 units ahead, radius 360, long lifetime, zero decay, RGB `(1,.2,.1)`, non-cone falloff. Existing GPU lighting consumes it. Baked lighting remains visible when disabled. AO differences surround torch entities, consistent with the shader's world/scene visibility ratio (`Shaders/ssao_evaluate.inc:348`). Baseline unlit return has only tiny quantization residue (maximum 28 one-level pixels), below the explicit diagnostic tolerance; off images exactly match their later unlit controls.

**Artifacts:** `/tmp/qsvr-toggle-final-qualification-e8z8zypp/receipt.json` aggregates all passing final modes; `evidence-contact.png` shows actual output and AO differences amplified 12×. Full captures, hashes, logs, and `pixel-metrics.json` reside in:

- Desktop: `/tmp/qsvr-toggle-final-tasks-iaebfu7g/desktop/`.
- Left: `/tmp/qsvr-toggle-final-tasks-2g4s8afg/left/`.
- Right: `/tmp/qsvr-toggle-final-tasks-lnwv40v4/right/`.

Reproduction uses `/tmp/qsvr-toggle-final-check.py`, `--engine`, matching `--build-id`, dependency `--lib-dir`, and optional `--mode`. It copies the private GDB/window helpers into each artifact root. Earlier GDB menu-call hangs, an early Monado readiness assertion, and a post-capture probe shutdown assertion were harness failures; they were corrected privately. All three selected final runs exit normally, with no probe exception. Desktop's parent receipt reports an aborted early XR setup, but its desktop mode passed; the aggregate selects that completed mode only.

**Unknown:** Physical-headset presentation, saved user sliders, half-resolution VR evaluation, CPU-lightmap fallback, and other GPUs are not qualified. No basis was found for changing desktop AO or shared VR shaders. Diff/whitespace checks pass. No user profiles, focus, drivers, or foreign processes were touched; all owned final game/private Monado processes were cleaned up. No shared build or commit was performed.
