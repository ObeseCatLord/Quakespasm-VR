# Surface pathing. Run: tests/run-headless.sh tests/terrain_surface.gdb
# The player hovers in mid air (setpos leaves noclip on) in god mode, away from every surface.
# Expect with terrain_crawler_nav 2: crawlers gather on the footing nearest the player and, with
# terrain_crawler_pounce on, jump at them (terrain_nav: pounces > 0). Compare by starting with
# DRG_ARGS="+terrain_crawler_nav 1 +terrain_crawler_pounce 0".
set pagination off
set confirm off
set debuginfod enabled off
handle SIG32 SIG33 SIG34 SIG35 SIGPIPE SIGUSR1 nostop noprint pass
break SCR_UpdateScreen
run
ignore 1 300
continue
call (void)Cbuf_AddText("terrain_crawler_sense 0; map drgbox\n")
ignore 1 500
continue
call (void)Cbuf_AddText("god; setpos 0 0 -90 20 0 0; terrain_crawler_sense 3000; echo PHASE t0; terrain_probe; terrain_ents\n")
ignore 1 1500
continue
call (void)Cbuf_AddText("echo PHASE t1; terrain_ents; screenshot\n")
ignore 1 1500
continue
call (void)Cbuf_AddText("echo PHASE t2; terrain_ents; screenshot\n")
ignore 1 1500
continue
call (void)Cbuf_AddText("echo PHASE t3; terrain_ents; terrain_probe; screenshot\n")
ignore 1 10
continue
call (void)Cbuf_AddText("condump\n")
ignore 1 5
continue
call (void)Cbuf_AddText("quit\n")
ignore 1 5
continue
