# CSQC command-input migration review

The inherited OpenVR client calls `CSQC_Input_Frame` on each prepared outgoing
command. The `2.0` adapter registers that hook and reuses the existing QuakeC
input globals at the vkQuake `CL_SendCmd` boundary. It retains vkQuake's float
`input_cursor_entitynumber` and also accepts the QSS-M integer declaration.
The callback changes ordinary command fields; native OpenXR hand and roomscale
samples remain owned by the command producer. After QC, the existing
uncalibrated-controller attack gate is reapplied.

An Astra xhigh read-only review of the first adapter found two P2 interactions:

| Finding | Disposition |
| --- | --- |
| A once-per-host-frame guard left later public catch-up commands unfiltered. | Removed. Every outgoing command invokes QC, as in the inherited client; stereo eyes do not create commands. |
| Prediction's unsent preview rebuilt raw movement and buttons even when QC filtered sent commands. | For mods with this hook, replay filtered committed command history but omit the speculative unsent step. Other prediction paths remain unchanged. |

The second choice is a deliberate responsiveness tradeoff for hook-bearing
mods. The callback can be stateful and reads command sequence, time and
duration. A cached filter cannot exactly predict its result for future input,
while calling it on preview would execute mod state a second time. An exact
preview would require a separate side-effect-free mod callback or a broader
command-lifecycle contract; neither exists in the inherited behavior.

The Linux `vkquake` build passes after the dispositions. Runtime acceptance
still needs a representative CSQC mod that changes movement/buttons, with
public catch-up and private predictive sessions, plus a desktop/VR HUD check.
This review does not close the full P1 gameplay or mod parity gate.

## Current callback ownership checkpoint — 2026-09-30

Main re-read the actual outgoing-command path and primary51b452c0 equivalent:
`cl_main.c:2713–2726` invokes CSQC_Input_Frame for each prepared command and
reapplies native attack admission afterward. At `cl_main.c:1937–1942`, a
hook-bearing mod omits the speculative unsent preview while retaining filtered
committed history. The once-per-render-frame requirement applies to HUD/view
presentation; it must not collapse distinct outgoing command callbacks.

Native `sbar.c:882–919` owns DrawHud/DrawScores and publishes the current display,
clock/player/intermission inputs. `gl_screen.c:2599–2614` submits one GUI task
with existing setup/draw dependencies; the serial path calls the same owner
once. Stereo uses the native panel display override and layered draw rather
than re-executing QC for each eye. Error cleanup at `gl_screen.c:2290–2310`
restores panel/display/item state and unlocks the actual QC mutex before VM
teardown. Source wiring is not a concurrency or authored-HUD runtime pass.

Separate scope fact: QSS-M has raw CSQC_InputEvent key/mouse dispatch at
`keys.c:2750–2766` and `in_sdl.c:1756–1781`. Pinned primary has no such owner;
pinned vkQuake and current2.0 only declare the hook in progs.h. A declaration
is not implemented event support. This broader QSS-M facility is not an
inherited command-filter regression and is not implemented by this checkpoint;
the full CSQC prediction APIs remain an unselected optional candidate. Keep
native keyboard/mouse ownership unless that separate extension is selected.

Final Linux/ARM checks must distinguish per-command filtering, once-per-frame
HUD draws, task/serial and desktop/stereo paths, preview policy, callback errors
and actual mod output. No builds/tests/compiler/probes/fixtures were run for
this inspection. VR demos are user-excluded; ordinary live input stays required.
