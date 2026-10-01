# Restore missing VR bindings at the explicit defaults boundary

2026-09-30. Before-code MOD-013/VR-009 plan. Production baselinebc7469f0;
voice.c is independently owned and outside this slice. User-dirty migration
document and main branch stay untouched.

## Verified behavior and narrow gap

Requested-Astra source audit compared native default restoration with retained
primary51b452c018273647dcf94f4628a370267ff8fa91. After the first XR sample has
latched automatic defaults, exec default.cfg or the menu's reset-defaults action
unbinds controls and restores the packaged desktop list, missing VR_ALTFIRE,
BBUTTON/XBUTTON and other VR controls. The first-sample filler will not run again.
The primary's compiled default config explicitly supplies these controls and
its unbind helper restores VR defaults; copying that unbind policy would prevent
deliberate ordinary unbinds on the native base.

Current VR_InputDefaultBindings_f/vr_defaultbindings already provides the
narrow action and VR_InputApplyDefaultBindings fills only absent/empty bindings.
It leaves native nonempty desktop/gamepad and mod/user assignments authoritative.
The existing vr_default_bindings_applied latch controls the one initial fill.
Native menu reset uses Cbuf_AddText(resetcfg, exec default.cfg). The built-in
default asset is Misc/vq_pak/default.cfg, listed in vq_pak_contents.txt and
embedded through native Meson mkpak/bintoc dependencies. No generated header
replacement or custom packaging/build owner is needed.

## Adapter and alternatives

Append the existing vr_defaultbindings command to the built-in default script
after its ordinary bindings/settings, and append it after exec default.cfg in
the native menu reset sequence. The menu completion handles a mod-provided
default script as well. Both calls are idempotent missing-only filling; no new
command or interpreter/filename hook. Later explicit unbind/custom assignments
remain authoritative. The default script's duplicate wheel binding is native
source-present and needs no rewrite.

Adjust the existing command handler to re-arm its existing one-shot latch when
no tracked session is available; if a tracked session is available, fill now
and mark applied. This covers an explicit reset while desktop/VR-unfocused,
followed by the next XR sample, without repeatedly restoring user unbinds each
frame or session. Ordinary desktop key assignments remain unchanged at reset;
deferred filling follows the current first-sample missing-only policy.

Reject restoring defaults inside every unbindall, globally replacing desktop
gamepad defaults with VR bindings, duplicating the binding table in assets,
or adding another settings/profile/input owner. Preserve the current automatic
startup policy, native command ordering/source admission and release/neutral
gates. Full controller-profile/default parity remains separate qualification.

Expected production write set Misc/vq_pak/default.cfg, Quake/menu.c,
Quake/vr_input.c only, roughly5–15 lines. Main verified the existing command,
helper/latch, menu and embedded-asset owner. Review the exact patch with bounded
local Astra source advice; reopen if another binding/state/exec layer is needed.

## Final qualification

After all implementation, actual native command execution and embedded pak must
cover menu reset and direct built-in exec before/after first XR sample, active/
unfocused/desktop transitions, mod-provided default script, nonempty custom
bindings and subsequent unbindall/single-key unbind/custom assignment. Missing
VR controls refill only at explicit restoration/initial sample; deliberate later
unbinding must not refill on ordinary frames. Keyboard/mouse/gamepad defaults
remain native in desktop. Runtime key release/neutral/controller behavior and
readable UI are not proven by a script-text comparison.
No builds/tests/compiler/probes/game runs until all implementation is finished;
Linux/ARM packaging/software checks come last, Windows/user headset trials later.
