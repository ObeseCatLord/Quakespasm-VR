# Alkaline/LimJam axe migration on 2.0

The exact installed `alk` and `limjam` programs now offer `VR_WEAPON_CONTACT_PROFILE_ALK` under the existing `sv_immersive_melee` server policy. The client prepares the pinned `progs/v_alkaxe20fps.mdl` blade edge and uses the existing private contact queue; the server borrows the native `W_FireAxe` damage leaf and substitutes only its first physical trace. Chainsaws and desktop input retain native behavior. No parallel melee protocol or collision state machine was added.

The installed programs have SHA-256 values `8c7425de5ac44b26f83c3dbe30929f0847c07d284e92f7ae726500904cbd2537` (`alk`) and `e28dba3261e561246ff12c5678ebcea1dc6720798c3400277fcc6e40666b9eeb` (`limjam`). Exact function layouts and `OP_CALL4` trace sites are pinned. Both mods ship the same 96,428-byte axe MDL (CRC32 `3003ca78`); the ready-pose blade edge from vertices 74/77 is 21.52 Quake units long. The 32-unit command bound limits blade length, while the existing grip and reach traces enforce contact reach.

An Astra xhigh senior review of the first integration found four behavior gaps. Source evidence was checked against the installed assets and donor before applying these decisions:

| Review finding | Disposition |
| --- | --- |
| LimJam's installed `vr_weapons.txt` omitted the selected Alkaline axe model, so command preparation had no muzzle calibration. | **Adopt:** add the donor Alkaline axe values as a game-scoped fallback for `alk` and `limjam`; authored schema entries still override it. |
| Consumed strokes could remain locked until the hand nearly stopped. | **Adopt:** Alkaline rearms below strike speed or on substantial reversal of the same recorded endpoint; a new arc and native cooldown are still required. Stock axe behavior stays on its existing path. |
| The shared stock sweep admitted handle-only contact and skipped initial blade overlaps. | **Adopt:** select the existing edge-only sweep and overlap recovery for Alkaline. |
| Borrowed QuakeC callback cleanup checked VM/program pointers but not edict/global storage, and the queued caller omitted the axe from post-callback validation. | **Adopt:** check saved storage after native calls and before restoration; report Alkaline callback entry to the shared queued-contact guard, including relocation and ownership invalidation. An actual crash had not been observed. |

Linux `build-debug/vkquake` links. `tests/vr_alk_program_fixture.sh` admits both installed programs and rejects mutated hashes/trace opcodes. `vr_alk_calibration_fixture.c` passes under ASan/UBSan and checks game scoping, schema override, and clean reload. `git diff --check` passes. These checks do not prove a rendered VR swing, damage amount, repeated-contact feel, multiplayer reconciliation, or live callback relocation; those require the user's later device and gameplay testing. The server policy remains opt-in while contact qualification is incomplete.
