# The workshop tools on the weapon wheel (+vr_weaponmenu, desktop). Run like workshop_tools.gdb:
#   gdb -q -batch -x tests/workshop_weapon_wheel.gdb --args ./build-drg/vkquake -basedir <base> \
#     -novr -nosound -window -width 1280 -height 720 +skill 1
# with maps/drgbox.bsp (tests/maps/drgbox.map) and the stock start map under <base>/id1.
# Slots are picked through the desktop wheel stick (VR_WeaponMenu_SetDesktopStick), aimed at the
# slot whose label matches, then released with -vr_weaponmenu, so the impulse comes from the wheel.
# Expected, in the condump:
# - PHASE open: SLOT lines include GRAPPLE, PLATFORM GUN and LAVA SPRAYER (all active 0) next to
#   the progs weapons; exactly one SLOT is active (the progs weapon in hand). vr_weaponlist lists
#   the three tool rows as visible source=profile and no DISCOVERED row. The first screenshot
#   shows the three tool slots.
# - The client needs the server's workshop_flarecount, which can arrive ahead of terrain_init:
#   without the cl_terrain.c fix no tool row appears at all.
# - PHASE grapple: "PICK GRAPPLE" with a hover id equal to its slot id, then workshop_info reads
#   "tool grapple"; the SLOT dump has GRAPPLE as the only active slot.
# - PHASE again: picking GRAPPLE while it is out sends nothing; workshop_info still "tool grapple".
# - PHASE shotgun: picking SHOTGUN puts the tool away: workshop_info "tool none weapon 1".
# - PHASE platgun: PLATFORM GUN is picked: workshop_info "tool platgun"; PLATFORM GUN the only
#   active SLOT; the second screenshot shows the gun held. vr_weaponlist still has no DISCOVERED row.
# - PHASE clear: terrain_clear on drgbox ends the workshop: workshop_info "no workshop map
#   running" and vr_weaponlist lists the three tool rows as "unowned".
# - PHASE start (map start, not a workshop map): no SLOT line names a tool, and vr_weaponlist lists
#   the tool rows as "unowned". workshop_info says "no workshop map running". Third screenshot.
set pagination off
set confirm off
set debuginfod enabled off
handle SIG32 SIG33 SIG34 SIG35 SIGPIPE SIGUSR1 nostop noprint pass

define wheel_dump
  set $i = 0
  while $i < vr_weapon_menu_frame.count
    printf "SLOT %d id %d %s active %d selectable %d ammo %d\n", $i, vr_weapon_menu_frame.visible[$i].entry->id, vr_weapon_menu_frame.visible[$i].label, vr_weapon_menu_frame.visible[$i].active, vr_weapon_menu_frame.visible[$i].selectable, vr_weapon_menu_frame.visible[$i].ammo
    set $i = $i + 1
  end
  printf "SLOTS %d open %d\n", vr_weapon_menu_frame.count, vr_weapon_menu_open
end

define wheel_pick
  set $i = 0
  set $found = -1
  while $i < vr_weapon_menu_frame.count
    if (int)strcmp(vr_weapon_menu_frame.visible[$i].label, $arg0) == 0
      set $found = $i
    end
    set $i = $i + 1
  end
  if $found >= 0
    call (void)VR_WeaponMenu_SetDesktopStick(vr_weapon_menu_frame.visible[$found].center_x - glwidth * 0.5f, vr_weapon_menu_frame.visible[$found].center_y - glheight * 0.5f, 0.1f)
    printf "PICK %s slot %d id %d hover %d\n", $arg0, $found, vr_weapon_menu_frame.visible[$found].entry->id, vr_weapon_menu_hover_id
  else
    printf "PICK %s not on the wheel\n", $arg0
  end
end

break SCR_UpdateScreen
run
ignore 1 300
continue
call (void)Cbuf_AddText("terrain_crawlers 0; map drgbox\n")
ignore 1 40
continue
call (void)Cbuf_AddText("god; notarget\n")
ignore 1 200
continue
call (void)Cbuf_AddText("echo PHASE open; +vr_weaponmenu\n")
ignore 1 30
continue
call (void)Cbuf_AddText("screenshot\n")
ignore 1 5
continue
wheel_dump
call (void)Cbuf_AddText("vr_weaponlist\n")
ignore 1 5
continue
echo PHASE grapple\n
wheel_pick "GRAPPLE"
call (void)Cbuf_AddText("echo PHASE grapple; -vr_weaponmenu\n")
ignore 1 40
continue
call (void)Cbuf_AddText("workshop_info; +vr_weaponmenu\n")
ignore 1 30
continue
wheel_dump
wheel_pick "GRAPPLE"
call (void)Cbuf_AddText("echo PHASE again; -vr_weaponmenu\n")
ignore 1 40
continue
call (void)Cbuf_AddText("workshop_info; +vr_weaponmenu\n")
ignore 1 30
continue
wheel_pick "SHOTGUN"
call (void)Cbuf_AddText("echo PHASE shotgun; -vr_weaponmenu\n")
ignore 1 40
continue
call (void)Cbuf_AddText("workshop_info; +vr_weaponmenu\n")
ignore 1 30
continue
wheel_pick "PLATFORM GUN"
call (void)Cbuf_AddText("echo PHASE platgun; -vr_weaponmenu\n")
ignore 1 40
continue
call (void)Cbuf_AddText("workshop_info; vr_weaponlist; +vr_weaponmenu\n")
ignore 1 30
continue
wheel_dump
call (void)Cbuf_AddText("-vr_weaponmenu\n")
ignore 1 20
continue
call (void)Cbuf_AddText("screenshot\n")
ignore 1 5
continue
call (void)Cbuf_AddText("echo PHASE clear; impulse 2; terrain_clear\n")
ignore 1 30
continue
call (void)Cbuf_AddText("workshop_info; vr_weaponlist\n")
ignore 1 5
continue
call (void)Cbuf_AddText("echo PHASE start; map start\n")
ignore 1 40
continue
call (void)Cbuf_AddText("god; notarget\n")
ignore 1 100
continue
call (void)Cbuf_AddText("workshop_info; vr_weaponlist; +vr_weaponmenu\n")
ignore 1 30
continue
call (void)Cbuf_AddText("screenshot\n")
ignore 1 5
continue
wheel_dump
call (void)Cbuf_AddText("-vr_weaponmenu; condump\n")
ignore 1 10
continue
call (void)Cbuf_AddText("quit\n")
ignore 1 5
continue
