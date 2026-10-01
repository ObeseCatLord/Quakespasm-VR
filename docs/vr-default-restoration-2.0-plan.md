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

## Architecture reopened after exact-patch review

The requested-Astra review of the10-addition/2-removal draft found one P2:
after prior VR defaults, detached default reset re-arms the latch, then a later
explicit unbind is overwritten at the next sample. Nonempty bindings survive,
but intentional emptiness is indistinguishable to the missing-only filler.
The draft is not committed as production and is not accepted as complete.
The broader unbind guarantee is retained; no exception is silently adopted.

### Verified environment and options for the revised decision

| Fact | Evidence / confidence |
| --- | --- |
| Existing key owner stores both NULL and allocated empty strings; filler considers both missing. | Verified keys.c:688–714 and vr_input.c:3935–3943 source. Pointer comparison alone cannot detect unbind of an already-NULL key. |
| Native unbindall only calls Key_SetBinding for non-NULL keys. | Verified keys.c:742–750; observing those calls alone misses already-empty default keys. |
| Native gamepad uses the same ABUTTON/BBUTTON/XBUTTON/thumb key identities. | Verified in_sdl.c:527–542. Unconditional synchronous VR fill while desktop changes native desktop controls. |
| Existing one-shot latch starts false and becomes true after initial sample. | Verified vr_input.c:3954,4004–4009. Initial startup fill must remain distinct from a later explicit restoration. |
| Key layer already includes vr_input.h; menu clears use Key_SetBinding. | Verified keys.c includes and menu.c:4630. No new service/module boundary is necessary for bounded notifications. |
| Runtime/packaging behavior and effective reviewer settings. | Unverified; no execution checks and no effective model metadata available. Requested Astra parameters are explicit; advice is not certified senior signoff. |

Solo-operator decision, no new profile/settings service. Main lean: a temporary
pending-explicit-restoration flag plus one bounded mask over the11 existing
VR default entries, owned beside the current defaults latch. Arm the explicit
mode only after a prior initial fill (or an already-pending explicit reset);
initial boot/config loading retains the current startup policy. Reset this
mask at a new explicit defaults command. While pending, narrow notifications
from the existing Key_SetBinding owner mark touched default keys so later
intentional emptiness wins. An unbindall completion notification marks all
entries, including those already NULL. Observe explicit same-value empty
assignment before the key owner's equality fast path.

The next sample consumes a snapshot of exclusions with the existing fill,
retires pending restoration before its own bindings can notify it, and clears
the temporary mask afterward. Nonempty assignments already remain authoritative;
other default keys still restore. No second binding table/key state, session
profile, periodic refill or saved setting. Repeated explicit reset deliberately
starts a fresh restore; ordinary unbind does not create defaults.

Compare alternatives: cancelling the whole pending restore for one changed key
would strand all other missing controls; globally filling synchronously changes
desktop gamepad behavior; a new per-key settings/profile or generic mutation
registry is wider than this demonstrated11-key boundary. A documented pre-sample
exception contradicts the intended later-unbind contract. Challenge whether the
flag/mask is actually necessary or can be simplified at the existing owner.

Revised expected write set includes Quake/keys.c and Quake/vr_input.h alongside
the original three files, roughly35–65 extra observer/consumption lines. This
material estimate/scope increase is why architecture is reopened before any
additional code. Request local Astra source design advice to verify/critique the
pending-versus-startup distinction, NULL/equality/unbindall cases and delete
unnecessary state. Main synthesizes a disposition before delegated correction.
Do not re-review controller motion, voice, full config IO or prediction here.

### Revised design disposition before correction

Main spot-checked the requested-Astra advice against Key_SetBinding,
Key_Unbindall_f and M_UnbindCommand. The menu only visits existing matching
bindings, so action-level clearing otherwise misses a currently absent VR key.
This source review changes the adapter; no additional settings owner is needed.

| Recommendation | Disposition |
| --- | --- |
| Menu command clearing must include currently missing keys. | Adopt: one VR-owned command-clear notification from M_UnbindCommand, matching the existing eleven-entry table. Individual removed-key notifications remain. |
| Replace the existing boolean with three phases. | Adopt: startup pending, applied, explicit restoration pending. One bounded exclusion mask remains temporary; no second pending boolean. |
| Observe validated key assignments before the equality fast path. | Adopt: notifications only exclude entries during explicit pending restoration. They do not fill or create pending work. |
| Unbindall must cover already-NULL keys. | Adopt: a completion notification excludes the entire existing table while pending. |
| Consume exactly once before generated bindings notify the key owner. | Adopt: snapshot mask, mark applied, clear mask, then use the existing missing-only filler; an entirely excluded restoration is still consumed. |
| Preserve startup policy and native desktop controller behavior. | Adopt: detached explicit commands only become explicit pending after a prior initial fill, or while already explicit pending. Startup configuration remains startup pending. |

Correction ownership remains the five production files named above. The menu
notification joins the existing menu write scope. No saved mask, second table,
periodic refill, generic binding-mutation service or command registry is added.
The earlier two-boolean lean is superseded. Review the complete actual patch,
including later NULL/empty assignment, action clearing and unbindall after a
detached restore, before committing production.

Review provenance: requested gpt-6-astra at xhigh, read-only source advice.
Effective runtime settings metadata remains unavailable, so the required
certified senior-review routing cannot be verified here. This is not final-goal
signoff or execution qualification. Main owns the adopted architecture.
