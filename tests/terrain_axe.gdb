# Walks into rock, then holds the axe. Run: tests/run-headless.sh tests/terrain_axe.gdb
# Expect: the origin stops, then advances again once "ops" starts counting up, until the region edge.
set pagination off
set confirm off
set debuginfod enabled off
handle SIG32 SIG33 SIG34 SIG35 SIGPIPE SIGUSR1 nostop noprint pass
break SCR_UpdateScreen
run
ignore 1 500
continue
call (void)Cbuf_AddText("god; notarget; echo PHASE walk; +forward\n")
ignore 1 1500
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 400
continue
call (void)Cbuf_AddText("terrain_probe; echo PHASE axe; impulse 1; +attack\n")
ignore 1 500
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 500
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 500
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 500
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 500
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 500
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 500
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 500
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 500
continue
call (void)Cbuf_AddText("-attack; -forward; screenshot; condump\n")
ignore 1 8
continue
call (void)Cbuf_AddText("quit\n")
ignore 1 5
continue
