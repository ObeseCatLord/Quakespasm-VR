# Menu graphics, installed-browser, and foveation plan

2026-10-03. This plan is deliberately limited to `Quake/menu.c`. It does not
change renderer policy, add-on transport, game switching, or assets.

## Evidence and behavioral reference

The native installed browser is already present: `M_Menu_Mods_f` resets to the
Installed view and calls `M_Mods_RefreshInstalled`; that function calls
`Modlist_Rebuild`, re-sorts the copied list, preserves a selected gamedir when
possible, and reapplies the filter. `Modlist_Rebuild` itself remains owned by
`host_cmd.c`. The same menu switches to the existing add-on catalogue, retains
its approval snapshot before installation, and installed activation queues the
existing `playgame` path. These owners must remain the only scanner, catalogue,
and launch path.

The reason the browser is hard to discover is in the main menu: it adds the
Mods selection only if `Get_Menu2` found `gfx/mainmenu2.lmp`, which in turn
requires a registered base-game session. The fallback baked `mainmenu.lmp`
therefore exposes only its original five rows despite `menu_mods` and the
installed browser already existing.

The read-only current `../quakespasm-openvr` master makes Mods a first-class
main-menu item and enters its installed list via `Modlist_Rebuild`. The
read-only current `../ironwail` master keeps its Mods UI separate from list
ownership and initializes from its existing `modlist`. The target already has
the more complete list/filter/catalogue implementation, so copying either
reference browser would duplicate state and weaken the existing approval flow.

The target Graphics page already exposes normal vkQuake controls, including
multisampling, anisotropy, water, transparency, models, particles, and
ray-query shadow quality (`off`, `low`, `medium`, `high`). `r_dynamic` is an
archived renderer cvar with default `1`, but has no row. The renderer now owns
SSAO compatibility through `R_SSAOSupported()` and owns the archived
`r_ssao_radius`, `r_ssao_strength`, and `r_ssao_vr_half` cvars. The existing
one-page menu would clip practical AO controls, so a compact General/Effects
page split is needed without changing any defaults.

The VR page formerly had independent Eye Tracking and Foveation rows. The
existing policy interprets foveation as Off/Fixed/Eye-tracked and explicitly
does not fall back from eye mode to fixed mode. Renderer policy now treats the
foveation mode itself as authoritative, including for console and stale saved
settings, so the menu must write only `vr_foveation`.

## Chosen incremental change

1. Keep the existing `mainmenu2.lmp` arrangement unchanged when available.
   For the normal fallback artwork only, append a plainly drawn `Mods` row and
   route it to the same `M_Menu_Mods_f`. This is an entry point, not a new
   browser or launcher. It preserves Help/Quit positions and remains usable
   without the optional menu asset. Its mouse row count and arrow wrap use the
   same six-entry ordering as its selection dispatch.
2. Split Graphics into keyboard page-switched General and Effects pages. The
   Effects page adds Dynamic Lights and, when `R_SSAOSupported()` says the
   renderer can run it, AO quality plus Radius (1..128) and Strength (0..1)
   sliders. During stereo rendering only, it also exposes `r_ssao_vr_half` as
   `Full`/`Half`; desktop hides this control because the renderer ignores it.
   Existing defaults, including `r_dynamic = 1`, radius 32, strength 1, and
   full-resolution VR AO, remain unchanged. Shadow quality stays capability
   gated by ray query.
3. Remove the standalone Eye Tracking menu row and write only the existing
   foveation selector value: Off, Fixed, or Eye-tracked. No menu-side
   automatic or fixed fallback is introduced.

## Rejected rewrite

Replacing the current Mods page with the OpenVR or Ironwail browser would
duplicate filtering, list lifetime, installed-refresh behavior, catalogue
approval snapshots, pointer controls, and the existing `playgame` boundary.
It has no demonstrated compatibility advantage. The chosen entry-point adapter
is the smallest end-to-end proof: main-menu selection reaches the existing
installed list, whose opening refresh discovers installed add-ons.

## Verification

No build, runtime, network, or deployment work is authorized. Verify by
source/diff inspection that fallback and enhanced main-menu selections both
reach `M_Menu_Mods_f`; that this function still calls
`M_Mods_RefreshInstalled`/`Modlist_Rebuild`; that Dynamic Lights toggles only
`r_dynamic`; that AO availability is determined only by `R_SSAOSupported()`;
that the three private SSAO cvars are read/written by name at the menu
boundary; and that foveation transitions write only `vr_foveation` while
leaving renderer policy untouched.
