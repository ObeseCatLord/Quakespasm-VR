# Vanilla monster footing. Run: tests/run-headless.sh tests/terrain_monsters.gdb
# The player stands still in god mode and every monster is told to hunt them. Compare each
# monster's "dist" between the first and last listing, and the terrain_steps counts, against a
# run with terrain_monsterstep 0 (the BSP footing rule).
set pagination off
set confirm off
set debuginfod enabled off
handle SIG32 SIG33 SIG34 SIG35 SIGPIPE SIGUSR1 nostop noprint pass
break SCR_UpdateScreen
run
ignore 1 300
continue
call (void)Cbuf_AddText("terrain_crawlers 0; map drgbox\n")
ignore 1 500
continue
call (void)Cbuf_AddText("god; terrain_monsterstep 1; terrain_wake; echo PHASE t0; terrain_ents\n")
ignore 1 1200
continue
call (void)Cbuf_AddText("echo PHASE t1; terrain_ents; screenshot\n")
ignore 1 1200
continue
call (void)Cbuf_AddText("echo PHASE t2; terrain_ents; screenshot\n")
ignore 1 1200
continue
call (void)Cbuf_AddText("echo PHASE t3; terrain_ents; screenshot\n")
ignore 1 1200
continue
call (void)Cbuf_AddText("echo PHASE t4; terrain_ents; terrain_probe; screenshot\n")
ignore 1 10
continue
call (void)Cbuf_AddText("condump\n")
ignore 1 5
continue
call (void)Cbuf_AddText("quit\n")
ignore 1 5
continue
