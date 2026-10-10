# Generic custom mod bindings

## Goal and behavioral references

Preserve vkQuake controls and mod-authored `bindlist.lst` labels. Let users assign
mod commands when no list is provided, on desktop, gamepad and OpenXR controls.
Sacrilege supplies paired `+hook`/`-hook` aliases (impulses 24/25) but no binding
list; that is an evidence case rather than an engine-specific exception.

## Minimal design

Keep the existing keys menu, command/alias registry, `Key_SetBinding`, key event
press/release processing and native/global-profile configuration saves. Add a
custom-command editor and then reuse normal capture. Display existing custom
assignments from the authoritative saved keybinding array, deduplicating rows
already described by authored or standard entries. Preserve authored labels.

Do not introduce a binding registry, metadata file written into mods, new XR
actions or a separate save format. Existing global aliases lack mod provenance
and can survive game changes, so do not guess which aliases belong to a mod or
assign keys automatically. The user chooses the command explicitly.

Command entry needs bounded text, exact assignment without the existing short
queued bind string, and validation appropriate to the existing quoted config
format. A paired button alias must preserve release semantics. Cancellation must
leave previous assignments unchanged, including the native two-bind limit.
Reuse mod-browser keyboard geometry for controller text entry, with a plus key,
and keep pointer-hit/capture routing explicit. Preserve physical keyboard input.

## End verification

After implementation: authored labels and ordinary rows remain; custom commands
can be added and cleared; existing saved commands remain visible; long commands
are not truncated; quotes/control input cannot break saved config; cancellation
preserves assignments; hooks execute both press and release; mod switch does not
relabel stale aliases as mod actions; desktop and logical VR buttons use the
same key owner; native save/reload and profile precedence preserve assignments.
Run the strict native build and a disposable initialized game/menu check. Actual
headset usability remains user validation. Cross-platform builds come last.

## Senior review

Local Astra verified the brief at explicit xhigh. Main spot-checked the hook
migration scheduling, keyup owner, command lookup and config serialization.

| Recommendation | Disposition |
| --- | --- |
| Retire automatic hook migration overriding postcfg choices | Adopt; delete the migration owner and its startup/game-change scheduling. Explicit bindings and existing default filling remain. |
| Retire capture-down before installing the new command | Adopt via existing `Key_Event(key, false)` before assignment; native stray-release guard then consumes the later physical release. |
| Defer old binding removal until successful capture | Adopt for native and custom capture. Copy the pending command independently of rebuilt rows. Cancellation and reopening clear draft/capture without touching assignments. |
| Exact command grammar | Adopt: trim outer spaces, limit new drafts to 255 bytes with visible overflow rejection; reject quotes, controls, semicolon scripts and comment delimiters; bare paired +commands only. Imported commands/macros remain verbatim. |
| Case-compatible command lookup and cvars | Adopt existing case-insensitive command/alias lookup and cvar owner. Server actions can use native `cmd action`; no new forwarding. |
| Reconstruct existing custom assignments | Adopt exact-string dedup after authored/native rows; unsupported saved strings remain editable/clearable without authored-file validation. |
| Do not add alias provenance or another registry | Adopt; no automatic alias-to-mod guesses. Unassigned temporary rows need not persist. |
| Verify capture interruption and VR hit transitions | Adopt end checks for Shift-Escape/reopen, focus loss, three old bindings, trigger misses/held activation, real +/- and save/reload. |

## Using custom controls

Load the mod, open Options → Controls → Custom command…, enter the command,
then select Bind (or press Enter) and press the desired key/controller button.
For Sacrilege enter `+hook`; the existing aliases provide its release action.
Desktop accepts typed text, and controllers use the onscreen keyboard. The
activation press only starts capture; release it before pressing the button to
assign. Commands already bound appear in the list, while mod-authored entries
retain their labels. Backspace/Delete clears an assignment; Escape cancels.

Native commands and aliases follow the engine's case-insensitive lookup; cvars
retain their case-sensitive names. New paired `+commands` must have a matching
`-command` and no arguments. Single commands may have arguments, including
`cmd action` for server forwarding. Existing saved macros remain intact.

## End-check results

The strict native Debug build passed. Eight production-menu/parser/registry
checks passed with ASan/UBSan: authored labels/dedup, command validation,
rejected drafts, control input, overflow recovery, cancellation/capture
ordering, long imported macros and menu lifecycle routing. Their key/command
owners are recording stubs and provide no native alias or save proof.

A separately initialized Sacrilege instance used the real menu, key dispatch,
aliases and config owners. Four scenarios passed: save and fresh-process reload
with normal config and with `-postcfg`/`-writepostcfg`. Capture did not execute
the new hook's release action, gameplay press/release produced impulses 24/25,
cancel preserved three existing assignments, successful replacement cleared
them, reopening cleared interrupted capture, 255-byte commands saved verbatim,
and switching id1/Sacrilege preserved explicitly configured hook assignments.
Rendered controls/editor screenshots fit the existing 320×200 menu canvas.
Installed game assets/settings were not modified by these private-profile tests.

Physical headset pointer/trigger usability remains user validation. The native
test exercises the logical VR button through `Key_Event`, without an XR session;
it does not claim gaze or spatial pointer evidence.
