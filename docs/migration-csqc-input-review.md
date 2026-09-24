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
