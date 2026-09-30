# Declared held-hook action on the existing VR secondary binding

2026-09-30. A remaining inherited behavior found during the command-source
audit, not a requirement to preserve a migration-command spelling. Read-only
primary pin51b452c0 vr.c:133–179 loads the active quake.rc, checks its +hook and
impulse24 declarations plus live +hook/-hook aliases, and assigns VR_ALTFIRE
only while empty/default. When the declaration is absent, only an exact stale
+hook binding is restored to the ordinary secondary action. No mod-name or
folder whitelist is used. Native VR input already supplies this secondary key;
the missing piece is the binding adapter after the mod's configs/aliases load.

## Verified owners and chosen adapter

Native vr_input.c:3895–3919 owns the default VR_ALTFIRE +button3 binding and
respects custom nonempty bindings. keys.c:688–714 releases an old held +action
before Key_SetBinding replaces it. Cmd_AliasExists is the existing alias owner;
COM_LoadFile/Mem_Free supply the active mod script. COM_SwitchGame queues
quake.rc then vid_unlock at common.c:3541–3542; client startup queues quake.rc
at host.c:1414. These are the existing post-config boundaries; do not add a
second config runner, parser, input route or persistent per-mod policy cache.

Copy the primary declaration helper and narrow VR-key assignment/cleanup into
vr_input.c using native file/allocator names. Register one small local command
for the existing startup/game-switch queues and explicit refresh. Queue it
after quake.rc's native execution boundary for clients only. Native exec's
inserted child configs complete before the queued adapter, preserving aliases
and the user's explicit bindings. Dedicated startup and reconnect/game-switch
owners remain otherwise unchanged; no local map launch follows this command.

Prepare the dormant VR-only binding even during desktop play. Source lookup
shows K_VR_ALTFIRE is emitted only by vr_input.c (:3788,3796), so this avoids
an extra late-session/config state machine while preserving ordinary desktop
input. A later compatible XR attachment can use the already prepared binding.
Keep native desktop Mouse2/config policy; do not import the primary's legacy
Mouse2 reset to +button3. That reset is not needed to route a virtual VR key.

Assign +hook only for a declared active script with both live aliases, and only
when the virtual key is empty or its native +button3 default. A nondefault user
binding remains authoritative. If the declaration disappears, reset only the
exact stale +hook virtual binding through Key_SetBinding, which already owns
held-action release. Do not unbind other keys, rewrite configs or infer hook
support from folder names. No per-frame script IO is needed.

## Scope and final acceptance

Write set: Quake/vr_input.c helper/command registration and client config-queue
lines in Quake/common.c and host.c. Estimate <=80 added lines. Main reviews
actual config insertion order and binding release/customization against the
primary; reopen for another owner or a materially larger implementation.
This plan precedes production code on2.0; main and user docs remain untouched.

After all implementation, Linux/ARM software checks cover declared/absent hooks,
live and absent aliases, default/empty/custom bindings, entering/leaving/same
game, held-action release, startup/game configs/user overrides, desktop then
compatible XR attachment and ordinary reconnect. No builds/tests/probes now;
live controller/gameplay and Windows verification stay user-deferred. This
adapter alone does not certify broader mod or migration completion.

## Source implementation checkpoint

The delegated implementation adds 34 lines in the planned three files. Main
review compared the helper with primary51b452c0 vr.c:133–179 and verified
native Cmd_Exec_f/Cbuf_InsertText insertion, client-only registration/startup,
and Key_SetBinding's old held-action release. Script and live-alias checks,
empty/default assignment and exact stale-hook cleanup match the chosen adapter;
ordinary desktop Mouse2 is untouched. Scoped whitespace review passed. No
builds, executable tests or controller trials have been run; the final checks
above remain open.
