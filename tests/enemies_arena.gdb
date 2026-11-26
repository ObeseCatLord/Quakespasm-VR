# The cave's enemies in the debug arena (tests/maps/drgarena.map: a flat terrain floor, nothing
# random). Run: tests/run-headless.sh tests/enemies_arena.gdb with DRG_ARGS="" and the arena map
# built into <basedir>/id1/maps (the script loads it itself).
# PHASE portraits: a yeti and a saucer placed in front of the player (screenshots).
# PHASE saucer: the saucer alone, woken against a god-mode player; the first terrain_enemies line
# shows lasers fired > 0 (its bolts replace the scrag's spit). PHASE yeti: the yeti alone; the last
# line shows snowballs thrown > 0 and snow drifts > 0 (snowballs that landed left snow as terrain
# ops), and terrain_clhash is equal on both sides.
# The yeti never casts lightning: its ranged attack is the smash's big snowball.
set pagination off
set confirm off
set debuginfod enabled off
handle SIG32 SIG33 SIG34 SIG35 SIGPIPE SIGUSR1 nostop noprint pass
break SCR_UpdateScreen
run
ignore 1 300
continue
call (void)Cbuf_AddText("terrain_crawlers 0; gl_cshiftpercent 0; map drgarena\n")
ignore 1 60
continue
call (void)Cbuf_AddText("god; notarget; crosshair 0; r_drawviewmodel 0; terrain_spawnenemy yeti -460 0 1 180\n")
ignore 1 60
continue
call (void)Cbuf_AddText("echo PHASE portraits; screenshot\n")
ignore 1 10
continue
call (void)Cbuf_AddText("terrain_clearmonsters all\n")
ignore 1 5
continue
call (void)Cbuf_AddText("terrain_spawnenemy saucer -480 0 60 180\n")
ignore 1 60
continue
call (void)Cbuf_AddText("screenshot\n")
ignore 1 10
continue
call (void)Cbuf_AddText("notarget; terrain_wake; echo PHASE saucer\n")
ignore 1 120
continue
call (void)Cbuf_AddText("screenshot\n")
ignore 1 700
continue
call (void)Cbuf_AddText("terrain_ents; terrain_clearmonsters all; terrain_spawnenemy yeti 200 0 1 180; terrain_wake; echo PHASE yeti\n")
ignore 1 200
continue
call (void)Cbuf_AddText("screenshot\n")
ignore 1 1300
continue
call (void)Cbuf_AddText("terrain_ents; terrain_clhash; screenshot\n")
ignore 1 10
continue
call (void)Cbuf_AddText("condump\n")
ignore 1 5
continue
call (void)Cbuf_AddText("quit\n")
ignore 1 5
continue
