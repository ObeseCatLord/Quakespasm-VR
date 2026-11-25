# Renders each r_terrain_style from three viewpoints plus a thrown flare: tests/run-headless.sh tests/terrain_styles.gdb
set pagination off
set confirm off
set debuginfod enabled off
handle SIG32 SIG33 SIG34 SIG35 SIGPIPE SIGUSR1 nostop noprint pass
break SCR_UpdateScreen
run
ignore 1 500
continue
call (void)Cbuf_AddText("god; notarget; developer 1; terrain_probe\n")
ignore 1 200
continue
call (void)Cbuf_AddText("r_terrain_style 0\n")
ignore 1 40
continue
call (void)Cbuf_AddText("echo SHOT style0; screenshot\n")
ignore 1 4
continue
call (void)Cbuf_AddText("r_terrain_style 1\n")
ignore 1 40
continue
call (void)Cbuf_AddText("echo SHOT style1; screenshot\n")
ignore 1 4
continue
call (void)Cbuf_AddText("r_terrain_style 2\n")
ignore 1 40
continue
call (void)Cbuf_AddText("echo SHOT style2; screenshot\n")
ignore 1 4
continue
call (void)Cbuf_AddText("r_terrain_style 3\n")
ignore 1 40
continue
call (void)Cbuf_AddText("echo SHOT style3; screenshot\n")
ignore 1 4
continue
call (void)Cbuf_AddText("terrain_flare\n")
ignore 1 200
continue
call (void)Cbuf_AddText("r_terrain_style 3\n")
ignore 1 10
continue
call (void)Cbuf_AddText("echo SHOT flare3; screenshot\n")
ignore 1 4
continue
call (void)Cbuf_AddText("r_terrain_style 0\n")
ignore 1 10
continue
call (void)Cbuf_AddText("echo SHOT flare0; screenshot\n")
ignore 1 4
continue
call (void)Cbuf_AddText("r_terrain_style 1\n")
ignore 1 10
continue
call (void)Cbuf_AddText("echo SHOT flare1; screenshot\n")
ignore 1 4
continue
call (void)Cbuf_AddText("r_terrain_style 2\n")
ignore 1 10
continue
call (void)Cbuf_AddText("echo SHOT flare2; screenshot\n")
ignore 1 4
continue
call (void)Cbuf_AddText("+right\n")
ignore 1 40
continue
call (void)Cbuf_AddText("-right\n")
ignore 1 2
continue
call (void)Cbuf_AddText("r_terrain_style 0\n")
ignore 1 10
continue
call (void)Cbuf_AddText("echo SHOT turn0; screenshot\n")
ignore 1 4
continue
call (void)Cbuf_AddText("r_terrain_style 1\n")
ignore 1 10
continue
call (void)Cbuf_AddText("echo SHOT turn1; screenshot\n")
ignore 1 4
continue
call (void)Cbuf_AddText("r_terrain_style 2\n")
ignore 1 10
continue
call (void)Cbuf_AddText("echo SHOT turn2; screenshot\n")
ignore 1 4
continue
call (void)Cbuf_AddText("r_terrain_style 3\n")
ignore 1 10
continue
call (void)Cbuf_AddText("echo SHOT turn3; screenshot\n")
ignore 1 4
continue
call (void)Cbuf_AddText("+left\n")
ignore 1 90
continue
call (void)Cbuf_AddText("-left\n")
ignore 1 2
continue
call (void)Cbuf_AddText("r_terrain_style 0\n")
ignore 1 10
continue
call (void)Cbuf_AddText("echo SHOT back0; screenshot\n")
ignore 1 4
continue
call (void)Cbuf_AddText("r_terrain_style 1\n")
ignore 1 10
continue
call (void)Cbuf_AddText("echo SHOT back1; screenshot\n")
ignore 1 4
continue
call (void)Cbuf_AddText("r_terrain_style 2\n")
ignore 1 10
continue
call (void)Cbuf_AddText("echo SHOT back2; screenshot\n")
ignore 1 4
continue
call (void)Cbuf_AddText("r_terrain_style 3\n")
ignore 1 10
continue
call (void)Cbuf_AddText("echo SHOT back3; screenshot\n")
ignore 1 4
continue
call (void)Cbuf_AddText("terrain_probe; condump\n")
ignore 1 8
continue
call (void)Cbuf_AddText("quit\n")
ignore 1 5
continue
