# Drives a windowed client through spawn, dig, fill and screenshots, one console line per N frames.
# gdb -q -batch -x tests/terrain_runtime.gdb --args <vkquake> -basedir <dir with id1> -novr -nosound -window +map start
# Expect "terrain:" lines with hash 6a1673dea18502c6 for seed 1 and five "Wrote" screenshots in id1.
set pagination off
set confirm off
set debuginfod enabled off
handle SIG32 SIG33 SIG34 SIG35 SIGPIPE SIGUSR1 nostop noprint pass
break SCR_UpdateScreen
run
ignore 1 400
continue
call (void)Cbuf_AddText("noclip; god; notarget; r_drawworld 0; r_drawentities 0; r_drawviewmodel 0; crosshair 0; viewsize 120; fov 100; developer 1; setpos 0 0 0 0 0 0\n")
ignore 1 20
continue
call (void)Cbuf_AddText("terrain_spawn 1; terrain_bench\n")
ignore 1 10
continue
call (void)Cbuf_AddText("screenshot\n")
ignore 1 10
continue
call (void)Cbuf_AddText("setpos 0 0 0 0 90 0\n")
ignore 1 10
continue
call (void)Cbuf_AddText("screenshot\n")
ignore 1 10
continue
call (void)Cbuf_AddText("setpos 0 0 0 25 200 0\n")
ignore 1 10
continue
call (void)Cbuf_AddText("screenshot\n")
ignore 1 10
continue
call (void)Cbuf_AddText("terrain_dig 90\n")
ignore 1 3
continue
call (void)Cbuf_AddText("terrain_dig 90\n")
ignore 1 3
continue
call (void)Cbuf_AddText("terrain_dig 90\n")
ignore 1 3
continue
call (void)Cbuf_AddText("terrain_dig 90\n")
ignore 1 10
continue
call (void)Cbuf_AddText("screenshot\n")
ignore 1 10
continue
call (void)Cbuf_AddText("terrain_fill 40\n")
ignore 1 10
continue
call (void)Cbuf_AddText("screenshot\n")
ignore 1 10
continue
call (void)Cbuf_AddText("quit\n")
continue
