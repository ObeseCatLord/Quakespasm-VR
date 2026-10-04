# vkQuake `749b4fd4` ordinary merge plan for `2.0`

Status: **authorized implementation complete; compiled merge candidate** (2026-10-04).

This plan was restored from its pre-merge stash without applying the stash.
The approved review dispositions and final implementation receipt are recorded
in [upstream-merge-dispositions-2.0.md](upstream-merge-dispositions-2.0.md).
Behavioral qualification is a separate main-integration gate before shipping.

## Fixed inputs and scope

- Integration branch and parent: `2.0` at `7cc8cdb2`.
- Merge base: `d336dbae0dc21b4693d34052c194ca6a32602e08`.
- Official locally available upstream tip: `749b4fd43fe8b8554b34163dc9469c4a08a4def1`.
- Upstream delta: 36 commits, 50 changed files, 2,214 insertions and 424
  deletions. The object is present in the local clone; no network fetch is
  required.
- `git merge-tree` identifies 17 files with 55 exact conflict-start markers. Forty-
  six changed-in-both files will also receive an explicit semantic audit.
- The original plan was the only pre-authorization workspace change. Engine
  implementation is confined to the isolated integration clone. Bonk gestures
  and texture-mip work remain independently owned for later main integration.
- A separate texture worker owns only rectangular-mipmap implementation, tests,
  and its documentation in another clone. This merge still imports the two
  upstream `gl_texmgr.c` lines as part of the 50-file historical merge, but it
  will not add, alter, or test rectangular-mipmap runtime behavior.

The integration will be one ordinary two-parent `--no-ff` merge. It will not
cherry-pick the 36 commits, replace files wholesale, use a global ours/theirs
strategy, or create parallel renderer, save, or protocol owners.

## Resolution boundaries

| Conflict file(s) | Resolution decision and retained owner |
|---|---|
| `common.c` | Retain upstream start-argument discovery and sky cache lifecycle. Keep the current game-reload calibration/menu hooks at the same filesystem-reset boundary, then order cleanup according to actual model/sky ownership. Do not replace search-path or VR reload policy. |
| `gl_draw.c`, `gl_screen.c` | Keep upstream cached-picture sizing and center-background behavior, adapting their calls through the existing physical/desktop canvas boundaries. Retain categorized graphics controls, 24-mod browser, native previews, tracked-pointer substitutions, and VR panel paths. |
| `gl_model.c` | Combine the current bounded copied-name termination with upstream unnamed BSP-texture fallback; preserve original source/destination bounds before applying the fallback. |
| `gl_rmisc.c`, `r_ssao.c`, `ssao_composite.inc` | Preserve upstream quality-specialized, full/half-resolution AO images, depth-aware lookup and desktop fast paths. Keep existing per-eye descriptors and stereo composite fetches. Model eye and resolution as independent axes; use the established pipeline owner for stereo-module selection. No duplicate AO state or descriptor graph. |
| `host.c`, `host_cmd.c`, `pr_edict.c`, `progs.h`, `server.h` | Keep exactly one immutable main-thread snapshot and background writer/scheduler. Add upstream autosave, commands, generic map filtering, wait-before-VM-teardown, atomic replacement and catalogue behavior to that owner. Preserve v5/v6/v7 dialect handling, co-op records/loaded clients, failure visibility, and the rule that workers never access QC/VM state. Reconcile declarations with the one save-data representation and scheduler. |
| `menu.c` | Reuse upstream preview/control, high-DPI/layout and `ui_mouse` behavior at native menu/input owners. Preserve the current graphics/mod rows and VR click/pointer helpers instead of bypassing either family. |
| `sv_main.c` | Adopt native split signon buffers and reservation calls. Keep current staged metadata-before2 draining, exact capacity admission, local/remote chunk policy, retries, permanent incompatibility reporting, and post-spawn userinfo at that sender owner. |
| `sv_phys.c` | Retain upstream movement corrections while keeping roomscale/head-pose values private to the existing command/movement boundary. Do not fork the predictive solver or leak private state into ordinary movement. |
| `sys.h`, `sys_sdl_unix.c` | Select one canonical native atomic rename/remove declaration and libc wrapper; keep current callers compatible without adding a filesystem abstraction. |

## Auto-merge audit

All 50 incoming paths and all 46 changed-in-both results were inspected,
including these groups:

- Desktop engine/UI: `cl_demo.c`, `cl_main.c`, `client.h`, `console.c`,
  `draw.h`, `gl_sky.c`, `gl_texmgr.c`, `gl_vidsdl.c`, `image.*`, `in_sdl.c`,
  `input.h`, `menu.h`, `screen.h`, `snd_dma.c`, `view.c`, `world.c`.
- Renderer/shaders: `gl_model.h`, `gl_rmain.c`, `r_passes.*`, `r_ssao.h`,
  `screen_effects.inc`, `ssao_{evaluate,filter,mip,prepare}.inc`, and
  `update_lightmap.inc`; verify current single-pass stereo, VR AO and protected
  output bindings remain reachable.
- VM/protocol: `pr_cmds.c`, `pr_ext.c`, `quakedef.h`; retain current private
  signon/network/head-pose contracts while accepting upstream desktop fixes.
- Platform: `sys_sdl_win.c`; retain upstream behavior and compile compatibility.

## Execution history and qualification gate

1. The ordinary `--no-ff --no-commit` merge was already in progress at the
   stated parent. It was continued without aborting, restarting or replacing
   the five partial resolutions already present.
2. All 17 conflict paths were resolved at the boundaries above. Save, signon,
   AO and menu changes reuse the existing native owners. The review's
   corrections to conflict counts and coupled contracts were adopted.
3. The candidate requires no unmerged paths, no conflict markers and a clean
   whitespace check before the merge commit.
4. Strict native compilation uses `make -C Quake DEBUG=1 USE_SDL3=1 -j4`
   after an isolated clean build. This graph uses `-Wall -Werror`; no
   production build graph was configured or changed.
5. The resulting ordinary two-parent merge object is the stable source base
   for independently owned acceptance fixtures and later Bonk integration.
   Final save, native signon, AO and user-observable behavior checks belong
   at implementation end. Candidate compilation does not establish shipping
   qualification; no shipping or deployment is performed by this merge.

The Astra review and main dispositions are complete and approved. The
primary Astra review's verified effective effort is **xhigh**. The final
receipt preserves the adopted decisions and their compatibility exceptions.
