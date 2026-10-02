# Strict Windows LTCG lifetime repair plan

2026-10-02, after actual d50a13a3 Release compilation/link attempt. Verified local
Astra/xhigh review checks six C owners and output helpers:41 unique summary
rows,42 native warnings including a separate guarded tracker binding in C++.
No /LTCG, WholeProgramOptimization, warning or native architecture changes.

| Recommendation | Disposition |
| --- | --- |
| Initialize selected small cached/outcome locals | Adopted. Values remain overwritten on admitted paths; existing admission/restoration guards remain owners. |
| Preserve saved_pmove conditional snapshot | Adopted. No unconditional large movement copy/clear. Only small saved_movevars gains zero initialization. |
| Avoid clearing entire private command/movevars | Adopted. Initialize buttons and pground fields used after control-flow gates; later full native assignments remain. |
| Preserve conditional PostThink movement input copy | Adopted with nullable pointer to the existing snapshot as its restore witness; callback order/owner conditions remain. |
| Zero skill/time alone | Rejected as insufficient. Legacy/KEX COM_ParseFloatNewline ignores sscanf failure. Compare returned cursor for conversion success and reject failed input before using output. Validate representable finite skill and finite saved time before their consumers. |
| Change global COM parser or rewrite load state machine | Rejected. Isolate demonstrated invalid-save incompatibility in existing Host_Loadgame_f; native valid parsing remains reused. |
| Extra C++ binding warning omitted from compact summary | Main spot-check: path helper returns success only after assigning handle. Initialize local binding=XR_NULL_PATH; no tracker/profile policy change. |

Main verified Deserialize publishes after complete validation, modelindex only
used with initialized brush model, yaw cache only becomes valid after successful
getters, conditional shadow snapshot and contact/context cleanup gates. Main
also inspected unchecked %f/newline/%n in COM_ParseFloatNewline and actual loader
consumers. The localized invalid-save refusal is a correctness repair, not a
compatibility fallback or protocol change. No general parser/builtin framework.

Luna/xhigh implements explicit source edits in voice_settings/world/vr_input/
cl_main/sv_phys/vr_openxr. Main owns Host_Loadgame_f refusal and integration.
The engine/source output remains native vkQuake, with existing VR adapters.
After complete implementation: review exact diffs, strict fresh Release then
Debug actual native builds and all bundled PE/import/notice checks. Reuse the
existing CPU native graph for affected loaded save/error and common compilation
boundaries; no GPU/game-window/physical-device tests. Final matching Linux/ARM
artifact reconciliation and final integration signoff remain F10.

Implementation complete: main reviewed exact Luna edits and verified effective
gpt-6-luna/xhigh. Main guards failed skill/time cursor advancement plus finite
representable skill/finite time. Existing native Linux target compile/link0;
ten malformed legacy/KEX cases reject without replacing the connected world,
followed by valid native save recovery and all nine existing stock cases pass.
Private results: FastGames/qsvr-ltcg-native-aktdklk4. This is dedicated loopback
with prepared renderer signon, not GPU or arbitrary save/mod qualification.
