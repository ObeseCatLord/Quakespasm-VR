# Ledge vault. Run: tests/run-headless.sh tests/terrain_vault.gdb
# Builds a mound about 64 units high in front of the player (a jump clears about 44), turns the
# jetpack off, then holds forward and jump. Expect the probes' origin z to end about 60 above the
# first one, on top of the mound. With "terrain_vault 0" instead the player stays at the bottom.
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
call (void)Cbuf_AddText("god; notarget; terrain_jetpack 0; terrain_vault 1; setpos 0 0 -217 25 0 0\n")
ignore 1 30
continue
call (void)Cbuf_AddText("terrain_fill 64\n")
ignore 1 30
continue
call (void)Cbuf_AddText("setpos 0 0 -217 0 0 0\n")
ignore 1 10
continue
call (void)Cbuf_AddText("noclip\n")
ignore 1 60
continue
call (void)Cbuf_AddText("echo PHASE start; terrain_probe; +forward; +jump\n")
ignore 1 40
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 40
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 40
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 40
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 40
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 40
continue
call (void)Cbuf_AddText("-forward; -jump; screenshot\n")
ignore 1 60
continue
call (void)Cbuf_AddText("echo PHASE end; terrain_probe\n")
ignore 1 10
continue
call (void)Cbuf_AddText("condump\n")
ignore 1 5
continue
call (void)Cbuf_AddText("quit\n")
ignore 1 5
continue
