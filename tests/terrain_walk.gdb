# Walks, jumps and backs through a drgbox cave, printing terrain_probe lines; ends with condump (id1/condump.txt).
# gdb -q -batch -x tests/terrain_walk.gdb --args <vkquake> -basedir <dir with id1 and maps/drgbox.bsp> -novr -nosound -window +map drgbox
# Expect every probe to show "overlap 0" and a positive field value, and the origin to change in each phase.
set pagination off
set confirm off
set debuginfod enabled off
handle SIG32 SIG33 SIG34 SIG35 SIGPIPE SIGUSR1 nostop noprint pass
break SCR_UpdateScreen
run
ignore 1 500
continue
call (void)Cbuf_AddText("terrain_probe; echo PHASE forward; +forward\n")
ignore 1 12
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 12
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 12
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 12
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 12
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 12
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 12
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 12
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 12
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 12
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 12
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 12
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 12
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 12
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 12
continue
call (void)Cbuf_AddText("-forward; echo PHASE jump; +jump\n")
ignore 1 4
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 4
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 4
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 4
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 4
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 4
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 4
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 4
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 4
continue
call (void)Cbuf_AddText("-jump; echo PHASE back; +back\n")
ignore 1 12
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 12
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 12
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 12
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 12
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 12
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 12
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 12
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 12
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 12
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 12
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 12
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 12
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 12
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 12
continue
call (void)Cbuf_AddText("-back; condump\n")
ignore 1 5
continue
call (void)Cbuf_AddText("quit\n")
ignore 1 5
continue
