# The workshop tools. Run: tests/run-headless.sh tests/workshop_tools.gdb
# Desktop keys: on the first workshop map the client binds 9 (grapple), 0 (platform gun), - (lava
# sprayer) and F (flare) where nothing is bound; the bind lines show them. The flare button throws the wrist's
# four flares, then prints "No flares left" (terrain_ents lists the terrain_flare entities).
# Grapple first, from the start room (impulse 71): aimed up and held, the player is pulled toward
# the ceiling, so the origin z in the PHASE grapple terrain_probe line is well above PHASE start.
# Then the platform gun (impulse 70): aimed down, its shots land as gingerbread disks (three
# terrain ops each, so "ops" grows by a multiple of 3) and the client copy still matches the
# server (terrain_clhash). Screenshots show the light string and the platforms.
# Then the mouse wheel (impulses 10 and 12): with every weapon given (impulse 9 leaves the rocket
# launcher out), the WHEEL lines must read, in order: tool none weapon 64 (lightning), grapple,
# platgun, lavagun, none weapon 4096 (axe) (up1..up5; the game starts on weapon 32), then going down
# (down1..down4): lavagun, platgun, grapple, none weapon 64.
# (tests/workshop_lava_tool.gdb covers the lava sprayer itself: impulse 72 and the tank.)
set pagination off
set confirm off
set debuginfod enabled off
handle SIG32 SIG33 SIG34 SIG35 SIGPIPE SIGUSR1 nostop noprint pass
break SCR_UpdateScreen
run
ignore 1 300
continue
call (void)Cbuf_AddText("terrain_crawlers 0; map drgbox\n")
ignore 1 40
continue
call (void)Cbuf_AddText("god; notarget\n")
ignore 1 360
continue
call (void)Cbuf_AddText("echo PHASE keys; bind 9; bind 0; bind -; bind f; workshop_flare; workshop_flare; workshop_flare; workshop_flare; workshop_flare\n")
ignore 1 30
continue
call (void)Cbuf_AddText("echo PHASE flares; terrain_ents\n")
ignore 1 10
continue
call (void)Cbuf_AddText("echo PHASE start; terrain_probe; impulse 71; +lookup\n")
ignore 1 80
continue
call (void)Cbuf_AddText("-lookup; +attack\n")
ignore 1 40
continue
call (void)Cbuf_AddText("screenshot\n")
ignore 1 160
continue
call (void)Cbuf_AddText("echo PHASE grapple; terrain_probe; -attack\n")
ignore 1 120
continue
call (void)Cbuf_AddText("impulse 70; +lookdown\n")
ignore 1 60
continue
call (void)Cbuf_AddText("-lookdown; +attack\n")
ignore 1 12
continue
call (void)Cbuf_AddText("screenshot\n")
ignore 1 500
continue
call (void)Cbuf_AddText("-attack\n")
ignore 1 60
continue
call (void)Cbuf_AddText("echo PHASE platforms; terrain_probe; terrain_clhash; screenshot\n")
ignore 1 10
continue
call (void)Cbuf_AddText("echo PHASE wheel; bind mwheelup; bind mwheeldown; impulse 1\n")
ignore 1 20
continue
call (void)Cbuf_AddText("impulse 9\n")
ignore 1 20
continue
call (void)Cbuf_AddText("echo WHEEL start; workshop_info\n")
ignore 1 5
continue
call (void)Cbuf_AddText("impulse 10\n")
ignore 1 20
continue
call (void)Cbuf_AddText("echo WHEEL up1; workshop_info\n")
ignore 1 5
continue
call (void)Cbuf_AddText("impulse 10\n")
ignore 1 20
continue
call (void)Cbuf_AddText("echo WHEEL up2; workshop_info\n")
ignore 1 5
continue
call (void)Cbuf_AddText("impulse 10\n")
ignore 1 20
continue
call (void)Cbuf_AddText("echo WHEEL up3; workshop_info\n")
ignore 1 5
continue
call (void)Cbuf_AddText("impulse 10\n")
ignore 1 20
continue
call (void)Cbuf_AddText("echo WHEEL up4; workshop_info\n")
ignore 1 5
continue
call (void)Cbuf_AddText("impulse 10\n")
ignore 1 20
continue
call (void)Cbuf_AddText("echo WHEEL up5; workshop_info\n")
ignore 1 5
continue
call (void)Cbuf_AddText("impulse 12\n")
ignore 1 20
continue
call (void)Cbuf_AddText("echo WHEEL down1; workshop_info\n")
ignore 1 5
continue
call (void)Cbuf_AddText("impulse 12\n")
ignore 1 20
continue
call (void)Cbuf_AddText("echo WHEEL down2; workshop_info\n")
ignore 1 5
continue
call (void)Cbuf_AddText("impulse 12\n")
ignore 1 20
continue
call (void)Cbuf_AddText("echo WHEEL down3; workshop_info\n")
ignore 1 5
continue
call (void)Cbuf_AddText("impulse 12\n")
ignore 1 20
continue
call (void)Cbuf_AddText("echo WHEEL down4; workshop_info\n")
ignore 1 5
continue
call (void)Cbuf_AddText("condump\n")
ignore 1 5
continue
call (void)Cbuf_AddText("quit\n")
ignore 1 5
continue
