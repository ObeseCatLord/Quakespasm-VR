# Per-mod VR controller actions

The default XR bindings route five controls through `+vr_mod <slot> <fallback>`.
When the active game directory (matched case-insensitively) has an entry in
`vr_mod_actions[]` in `Quake/vr_input.c`, that command runs. Otherwise the
fallback runs. Nothing game-specific is written to saved configuration.

| Slot | Control (from `VR_InputBuildHandDesired`) | Fallback |
| --- | --- | --- |
| `grip_l` | Left grip, all controllers (`VR_LSHOULDER`) | `+showscores` |
| `altfire` | Index: right stick click. Other controllers: right grip (`VR_ALTFIRE`) | `+button3` |
| `grip_r` | Index right grip only (`VR_RSHOULDER`) | `+showscores` |
| `stick_l` | Left stick click, or Vive pad click (`VR_LTHUMB`) | `+speed` |
| `xbutton` | Weapon hand's primary button (`VR_XBUTTON`) | `+showscores` |

Other controllers never produce `grip_r`, so every mod gives it the same
command as `altfire`. The weapon wheel already handles weapon selection, and
`impulse 10`/`impulse 12` cycle weapons. Nothing is bound to `vr_turn180` by
default.

## Mapping rule

- `altfire` (and `grip_r`): the mod's MOUSE2 action, such as an offhand hook or
  secondary fire. A mod with no MOUSE2 action gets its flashlight here.
- `grip_l`: the mod's use, interact or hold action.
- `xbutton`: a mode toggle or an inventory display.
- `stick_l` stays on run unless a movement action clearly matters more
  (MCJ2 dash).
- Mods built on the same progs family use the same control for the same
  action.

None of the installed mods' server QuakeC reads `.button3`-`.button8`, so the
`+button3` alt-fire fallback reaches none of them. Their CSQC registers no
gameplay commands either.

## Mapped actions

| Mod (game dir) | Action | Control | Command | Evidence |
| --- | --- | --- | --- | --- |
| mcj2 | Toggle build mode | grip_l | `impulse 109` | earlier MCJ2 entry, `bindlist.lst`, `src/` |
| mcj2 | Place block | altfire, grip_r | `impulse 130` | same |
| mcj2 | Wind-charge dash | stick_l | `impulse 127` | same; `README.txt` "wind dash" |
| ad, gibtropolis, mc_hj26, hwjam2, hwjam4, peril3.0, q30a1024, quake_rooftop_jam_v2, ravenkeep, tcj | Show arcane key inventory | xbutton | `impulse 175` | `quake.rc` `bind "i" "impulse 175"`; progs string "Press 'i' Key to open Inventory"; `ImpulseCommands` handles 175 in every one |
| nyarlathotep, something_wicked, Tershibboleth (Drake) | Use worn special armor | grip_l | `impulse 20` | progs `ImpulseCommands` 20 calls `W_UseArmor`; string "Double-press JUMP to use armor." (double jump still works) |
| mjolnir (and mjolnir1.0) | Use / purchase | grip_l | `impulse 159` | `bindlist.lst` "use / purchase"; `default.cfg` `bind "e"`; the `trigger_coinpurchase` prompt asks for the use key |
| mjolnir | Flashlight | altfire, grip_r | `impulse 146` | `bindlist.lst` "flashlight"; `default.cfg` `bind "f"`; `ToggleFlashlight` |
| mjolnir | Power shield toggle | xbutton | `impulse 59` | `bindlist.lst` "power shield"; `default.cfg` `bind "l"`; string "Press 'L' to toggle." |
| immortal | Flashlight | altfire, grip_r | `impulse 13` | `immortal_readme.txt` (flashlight on 0, `impulse 13`); `quake.rc` alias `flashlight`; `toggleFlashLight` |
| rm1.2 (Re:Mobilize), sacrilege | Offhand light hook (hold) | altfire, grip_r | `+hook` | `quake.rc` `alias +hook "impulse 24"`, `alias -hook "impulse 25"`, `bind MOUSE2 +hook`; `sac_readme.txt` |
| vr (Violent Rumble) | Secondary fire | altfire, grip_r | `impulse 228` | readme "NEW IMPULSES"; `quake.rc` `bind "MOUSE2" "impulse 228"` |
| vr | Quick grapple (hold) | grip_l | `+qgrap` | readme; `quake.rc` `alias "+qgrap" "impulse 240"` / `"-qgrap" "impulse 241"` |
| vr | Switch firing mode / ammo type | xbutton | `impulse 227` | readme; `quake.rc` `bind "q" "impulse 227"` |

`+hook` and `+qgrap` are the mods' own aliases from `quake.rc`. `+vr_mod`
sends the matching `-` command on release.

## Actions left to the controls menu

There are more Mjolnir actions than free controls. These keep their
`bindlist.lst` labels in Options → Controls and can be bound there:
`impulse 56` Dragonscale (invulnerability), `impulse 57` Spirit Shroud
(invisibility), `impulse 58` Berserker (quad), `impulse 143`/`144`/`175` show
coins/bones/keys, `impulse 55` honk horn, `impulse 178` HUD style, and the weapon
slot 9/10 cycles (`impulse 90`/`77`; the wheel lists those weapons directly).

Violent Rumble's `impulse 22` (select grappling hook), `229`/`230` (rail zoom)
and `102` (rune count) are also left unmapped. Quick grapple covers the hook,
and the readme says zoom does not work in vkQuake.

## No mapping needed

| Mod | Reason |
| --- | --- |
| id1, bbelief, honey, dopa, mg1, lq1 | Stock impulses only (weapons, cycling, cheats). lq1's 200-203 are a camera tool. mg1/dopa 219 is a cheat. |
| hipnotic, rogue | Weapons are on the wheel. Rogue's grapple (`impulse 22`) is only selectable in deathmatch team play. Other impulses are cheats or dumps. |
| mg3 | Impulses 100-228 are cheats and dev toggles listed in the progs help strings. 225 laser is on the wheel. |
| AD grapple (all AD-family mods) | `weapon_grapplehook` replaces the axe and fires with attack (`W_FireGrapple` from `player.qc`). Jump detaches it (`client_ghook.qc` reads only button0/button2). Jump boots act through jump. |
| alk, limjam, enyo (Alkaline) | Impulses are settings toggles, debug and cheats (boots/wings cheats). Jump boots are passive. Extra weapons are on the wheel. |
| Copper-based: bonkjam, ctsj2, dwellv2p2 (and dwell), qbj3, reliq, smej2, snack3, spiritworld, tombofthunder, udob | Copper impulses: 13-16 best/last weapon, 17/18 crosshair/HUD, 20-40 map trigger hooks, 100+ dev and cheats. Bonk's hammer dash comes from the hammer attack, and its `impulse 144` only inspects the hammer. ctsj2's craftable grappling hook works through map entities. |
| sacrilege, rm1.2 (other impulses) | Apart from the hook, 222-224 give or remove jump boots (cheats), 13 gives keys, 66/100 are cheat/dev. |
| warpspasm | Quoth cheats only. Plasma gun (250) is on the wheel. No flashlight. |
| zer | 252-254 select weapons (on the wheel), 13 is a key cheat. Any impulse skips a cutscene, so the A/B weapon-cycle buttons already do. |
| qdoom | `impulse 55` is a dev/cheat (`enforcer_attack`). |
| Drake pets/modes | `impulse 51`-57 (pet commands) and 70-89 (game mode settings) are optional. Bind them from the controls menu if wanted. |

## Verification

- `tests/vr_default_bindings_fixture.c` checks the new `xbutton` default and the
  table's invariants: known slots, `grip_r` mirrors `altfire`, no duplicate
  rows, no turn command. It also checks case-insensitive lookup by game
  directory and that an unmapped game (id1) falls back on every slot.
- Runtime check on a hidden display with VR disabled. A cfg ran
  `+vr_mod <slot> echo FALLBACK` and the matching `-vr_mod` for every slot in
  id1, sacrilege, rm1.2, vr, Tershibboleth, ad, mjolnir, immortal and mcj2. Only
  the unmapped slots printed the fallback, and no mapped action produced
  "Unknown command". This checks table lookup and alias resolution, not the
  QuakeC effect of each impulse.
  In vr the check runs from `autoexec.cfg`, because its aliases are defined
  after `stuffcmds`.

Existing configurations keep any binding already saved on these controls. A
control saved as plain `+showscores` or `+button3` has to be rebound to the
`+vr_mod` form, or restored with `vr_defaultbindings` after unbinding it, before
the per-mod actions apply.
