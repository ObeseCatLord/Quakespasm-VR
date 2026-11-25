# Fires a rocket and a grenade and throws a flare. Run: tests/run-headless.sh tests/terrain_blast.gdb
# Expect: "ops" in terrain_probe goes up after each explosion; a terrain_flare line with overlap 0.
set pagination off
set confirm off
set debuginfod enabled off
handle SIG32 SIG33 SIG34 SIG35 SIGPIPE SIGUSR1 nostop noprint pass
break SCR_UpdateScreen
run
ignore 1 500
continue
call (void)Cbuf_AddText("god; notarget; impulse 9; terrain_probe\n")
ignore 1 30
continue
call (void)Cbuf_AddText("impulse 7; echo PHASE rocket; +attack\n")
ignore 1 20
continue
call (void)Cbuf_AddText("-attack; screenshot\n")
ignore 1 400
continue
call (void)Cbuf_AddText("terrain_probe; echo PHASE flare; terrain_flare\n")
ignore 1 150
continue
call (void)Cbuf_AddText("screenshot\n")
ignore 1 600
continue
call (void)Cbuf_AddText("terrain_ents; terrain_probe; screenshot\n")
ignore 1 10
continue
call (void)Cbuf_AddText("echo PHASE grenade; impulse 6; +attack\n")
ignore 1 20
continue
call (void)Cbuf_AddText("-attack\n")
ignore 1 1500
continue
call (void)Cbuf_AddText("terrain_probe; screenshot; condump\n")
ignore 1 8
continue
call (void)Cbuf_AddText("quit\n")
ignore 1 5
continue
