# Mod-compatible predictive VR movement boundary

Date: 2026-09-27. Branch: `2.0`. Astra senior design review ran with effective
`gpt-6-astra` / `ultra` settings. It reviewed the selected private movement
owner against the local QSS-M and inherited OpenVR implementations. The
selected trial remains default-off and restricted to the pinned stock QuakeC.

The objective remains predictive VR co-op on modded QuakeC, including
AD-family mods, without replacing vkQuake's renderer, native desktop physics,
or the selected command queue. The current selected owner runs PreThink,
scheduled weapon Think, PMove, impacts and triggers, physical contact, and
PostThink for each completed command (`Quake/sv_phys.c`,
`SV_Physics_ClientPrivateWalkTrial`). It then publishes the completed command
cursor. This command lifecycle is worth reusing.

| Finding / option | Disposition |
| --- | --- |
| Removing the stock progs identity check would immediately enable mods. | **Rejected.** The selected handoff removes presumed stock QuakeC water/jump velocity changes before PMove. Its dry jump branch can restore the entire earlier velocity, erasing a mod force. WALK hull and absent `customphysics` do not prove that a mod follows stock movement semantics. |
| Add a qualified mod movement adapter at the existing QC-to-PMove boundary. | **Adopted as the next design direction.** Qualify one exact installed progs identity and assign jump impulse, drag, flags, teleport/waterjump timers, and mod-authored forces to explicit owners. Identity identifies the qualification; it is not behavioral proof. Keep the existing selected queue and completion tail. |
| Invoke `SV_RunClientCommand` or copy QSS-M's movement body. | **Deferred / rejected respectively.** The 2.0 QC hook is declared but not invoked and `PF_sv_pmove` is not integrated. QSS-M's dispatch is a useful reference, but copying its command owner would duplicate vkQuake callbacks and lose the VR queue contract. A cooperative QC hook needs a defined movement/callback/replay contract first. |
| Use vkQuake native movement for unknown mods before selection. | **Adopted.** It preserves the working desktop/public fallback. Do not switch a selected command to native after some callbacks have already executed. |

Astra also found a concrete inconsistency in the preceding moving-platform
slice: selected admission and physics allowed robust BSP pusher ground, but
`SV_PrivateWalkTrialStateValid` in `Quake/sv_user.c` still rejected it on the
next packet. The packet gate now matches the `sv_gameplayfix_elevators >= 3`
condition while retaining its ground-offset validation. This fixes a
source-proven disconnect path; a stock lift still needs end-to-end proof.

The first mod software proof should pin one installed AD-family binary and
confirm its movement-code correspondence, then compare native and selected
dry WALK, jump, and a real mod-authored force. Record origin, velocity,
flags, timers, callback effects, and completed ACK. Compare client replay
separately with the existing shadow trace. Expand to water, teleport, and
moving supports only after that contract holds. For the lift regression,
send a **new** selected command after pusher ground has been committed; a
prequeued command alone will miss the old packet-gate defect. The user will
perform device and performance measurements later.
