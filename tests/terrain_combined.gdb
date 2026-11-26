# Everything that edits the cave in one run. Run: tests/run-headless.sh tests/terrain_combined.gdb
# The worm is on and hunting (its defaults) and the crawlers spawn as usual (god keeps the player
# alive). An ice pillar is cut so it falls as a body and is stamped back when it rests, lava is
# sprayed down the slope and melts the floor, the platform gun adds a gingerbread disk, then
# a console dig and fill add a few more ops. Expect at the end:
#   terrain_clhash: client <h> ops N, server <h>   with the two hashes equal and N equal to the
#   server's op count (terrain_probe "ops N");
#   terrain_ice_verify: ... detach >= 1, stamp >= 1 ... MATCH (the server log replays);
#   terrain_lava_info and terrain_worm_info show both ran (dig ops > 0); the verify line counts disk > 0.
set pagination off
set confirm off
set debuginfod enabled off
handle SIG32 SIG33 SIG34 SIG35 SIGPIPE SIGUSR1 nostop noprint pass
break SCR_UpdateScreen
run
ignore 1 300
continue
call (void)Cbuf_AddText("map drgbox\n")
ignore 1 40
continue
# god as soon as the map is up: the crawlers find the player within seconds
call (void)Cbuf_AddText("god; notarget\n")
ignore 1 460
continue
call (void)Cbuf_AddText("echo PHASE start; terrain_clhash; terrain_worm_info; setpos -200 0 -190 -75 0 0\n")
ignore 1 20
continue
call (void)Cbuf_AddText("terrain_ice_pillar 280; terrain_ice_info\n")
ignore 1 40
continue
call (void)Cbuf_AddText("terrain_ice_cut; echo PHASE cut\n")
ignore 1 30
continue
call (void)Cbuf_AddText("setpos 0 0 -200 20 0 0; +terrain_lava\n")
ignore 1 140
continue
call (void)Cbuf_AddText("-terrain_lava; screenshot; echo PHASE sprayed; terrain_lava_info; terrain_ice_info\n")
ignore 1 20
continue
call (void)Cbuf_AddText("terrain_worm_hunt; impulse 70; +lookdown\n")
ignore 1 60
continue
call (void)Cbuf_AddText("-lookdown; +attack; workshop_info\n")
ignore 1 300
continue
call (void)Cbuf_AddText("-attack; impulse 70; workshop_info\n")
ignore 1 200
continue
call (void)Cbuf_AddText("terrain_dig; wait; terrain_fill\n")
ignore 1 60
continue
call (void)Cbuf_AddText("echo PHASE end; terrain_lava_info; terrain_worm_info; terrain_ice_info; terrain_ice_verify; terrain_probe; terrain_clhash; screenshot\n")
ignore 1 10
continue
call (void)Cbuf_AddText("condump\n")
ignore 1 5
continue
call (void)Cbuf_AddText("quit\n")
ignore 1 5
continue
