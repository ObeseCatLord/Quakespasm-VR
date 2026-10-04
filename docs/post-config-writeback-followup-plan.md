# Launcher global-profile write-back follow-up

2026-10-04; plan before implementation. Keep native vkQuake global/game
configuration owners and the existing explicit postcfg startup order.

## Verified gap

The launcher `CommandBuilder` emits `-postcfg <quake.cfg> -writepostcfg`.
Its global config service explicitly expects engine writes at shutdown and
in-process mod changes. The legacy engine `Host_GetConfigWritePath` implements
that contract; this branch only reads postcfg and saves `vkQuake.cfg`.
Consequently the unchanged launcher profile overrides in-game edits at the
next launch. Installed AD/Rooftop game profiles and launcher `quake.cfg` also
save desktop `crosshair 0`; native gameplay screenshots confirm the crosshair
appears when explicitly enabled. No desktop crosshair renderer rewrite is
justified by this evidence.

## Minimal adapter

Reuse the legacy last-valid-argument and base-relative/absolute path policy.
Retain both vkQuake saves and supplement them with the opted-in launcher
profile save, using native binding/cvar writers and portable atomic replace.
Only an explicitly writable, successfully loaded final postcfg is eligible.
Read-only postcfg scripts remain read-only. Reject truncation and preserve old
profile bytes on failed writes. No launcher rewrite or second config store.

Astra xhigh senior review will challenge write eligibility, script ordering,
video serialization and preservation before code. Final validation uses
isolated native profiles across clean shutdown/relaunch and in-process mod
switch, repeated/relative/absolute postcfg, missing or unwritable paths, and
plain launches. Do not overwrite installed user settings during diagnostics.

## Adjacent command-line bound: reviewed before implementation

The native 256-byte reconstructed `cmdline` can discard late launcher `+map`,
`+connect`, identity and model commands although `com_argv` still contains the
postcfg option. The verified legacy producer and consumer both use 4096.
Astra (`gpt-6-astra`, effective xhigh verified) recommends restoring that
shared limit and rejecting incomplete batches rather than silently moving the
truncation boundary. Adopted: shared 4096 limit, explicit producer termination,
producer diagnosis/empty executable command reconstruction on actual overflow,
and bounds checks on both character and separator appends in `stuffcmds`.
The 50-argument option policy and existing command parsing remain unchanged.
Qualification includes exact 4095-byte fits, one-byte overflow, oversized ROM
replacement input and launcher-shaped command lines beyond 256 bytes.

## Senior-review disposition

| Recommendation | Disposition |
| --- | --- |
| Dispatch is earlier than script execution; an early game/writeconfig can overwrite the final profile. | Adopt an existing-generation completion marker after the batch; readiness clears on supersession. No separate script executor. |
| Cbuf_InsertText can discard oversized scripts without status. | Adopt bounded insertion accounting for script newline, marker and remaining queued text; any failed load/insertion disables write-back. |
| A predictable temporary opened with w can destroy a linked original. | Adopt exclusive wx creation; an existing temporary causes an untouched failure. Only the successfully created temporary may be removed. |
| VFS fallback must not authorize a different OS destination. | Adopt loader provenance; fallback-only scripts execute but remain read-only. |
| Writing into native global/game vkQuake.cfg conflicts with native split saves. | Adopt native file-identity conflict rejection with a diagnostic. Native saves retain ownership. |
| Restore legacy command-line capacity with defined overflow handling. | Adopt 4096 and producer/consumer guards, retaining existing parser and argument policy. |

Exclusive creation uses the existing Unicode-aware Sys_fopen wrapper. Its x
mode is supported by [Microsoft's CRT documentation](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/fopen-wfopen?view=msvc-170)
and [the Linux fopen manual](https://man7.org/linux/man-pages/man3/fopen.3.html).
No new platform-specific file service is needed. This is crash-safe replacement
against failed serialization/rename, not a power-loss durability guarantee.

## Completed native qualification

The coordinated assertion-enabled Linux engine builds successfully with the
adapter and restored 4096-byte limit. Eleven isolated real-engine cases pass:
repeated overrides in command-line order; an absolute profile containing spaces;
base-relative earlier override; nested exec/wait; changed settings through an
in-process AD-to-id1 switch and clean quit; subsequent launch; read-only override;
early writeconfig before batch completion; an early game switch; oversized final
script; existing symlink/directory temporary collisions; VFS-only script; missing
final profile; and a native vkQuake.cfg overlap. Several boundaries share a case.
Changed crosshair=1, AO=3, anisotropy=16, sensitivity=2.5 and the `t` voice binding
are present in the serialized global profile. Old read-only/failed input bytes
remain unchanged. Native saves continue. These are actual engine/GDB frame and
filesystem assertions, not a stand-in settings service.

Private receipts: `qsvr-postcfg-native-0751ug_c` and
`qsvr-postcfg-native-t8xq02le` under `/tmp`. Initial diagnostic attempts incorrectly
expected the unregistered voice-volume cvar under `-nosound`; the corrected
nested-exec witness uses the registered sensitivity setting. No production
assertion was weakened to compensate for an engine defect.

`tests/run_commandline_bounds.py` extracts the actual production producer and
consumer functions, compiles them with strict warnings and ASan/UBSan, and
passes 8192 argv-length cases, 8192 ROM command-length cases, exact fits,
one-byte overflows, separator expansion overflow and a late +map beyond the
previous 256-byte boundary. Only console/buffer sinks are captured. Native
launcher-shaped startup in the real-engine cases exercises the integrated path.
Physical VR, Windows runtime behavior and power-loss durability are not implied.
