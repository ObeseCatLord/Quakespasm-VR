# Jetpack, walk, axe and monster listings on drgbox. Run: tests/run-headless.sh tests/terrain_play.gdb
# Expect: jetpack probes climbing at up to 260, every probe "overlap 0", terrain_ent lines with onground 1 overlap 0.
set pagination off
set confirm off
set debuginfod enabled off
handle SIG32 SIG33 SIG34 SIG35 SIGPIPE SIGUSR1 nostop noprint pass
break SCR_UpdateScreen
run
ignore 1 500
continue
call (void)Cbuf_AddText("god; terrain_probe; echo PHASE ents0; terrain_ents; screenshot\n")
ignore 1 10
continue
call (void)Cbuf_AddText("echo PHASE jetpack; +jump\n")
ignore 1 10
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 10
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 10
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 10
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 10
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 10
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 10
continue
call (void)Cbuf_AddText("-jump; echo PHASE fall\n")
ignore 1 40
continue
call (void)Cbuf_AddText("terrain_probe; echo PHASE walk; +forward\n")
ignore 1 60
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 60
continue
call (void)Cbuf_AddText("terrain_probe; echo PHASE axe; impulse 1; +attack\n")
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
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 40
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 40
continue
call (void)Cbuf_AddText("terrain_probe\n")
ignore 1 40
continue
call (void)Cbuf_AddText("-attack; -forward; screenshot; echo PHASE ents1; terrain_ents\n")
ignore 1 200
continue
call (void)Cbuf_AddText("echo PHASE ents2; terrain_ents; terrain_probe; screenshot\n")
ignore 1 10
continue
call (void)Cbuf_AddText("condump\n")
ignore 1 5
continue
call (void)Cbuf_AddText("quit\n")
ignore 1 5
continue
