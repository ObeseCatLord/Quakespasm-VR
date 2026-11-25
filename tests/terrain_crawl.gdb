# Crawler prototype. Run: tests/run-headless.sh tests/terrain_crawl.gdb
# The player stands still in god mode. Expect: the last four monster_demon1 entries (the crawlers)
# close in between listings until they are in melee range.
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
call (void)Cbuf_AddText("god; terrain_crawler_sense 3000; echo PHASE t0; terrain_ents\n")
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
