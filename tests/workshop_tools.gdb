# The workshop tools. Run: tests/run-headless.sh tests/workshop_tools.gdb
# Platform gun (impulse 70): aimed down, four shots land as gingerbread disks (three terrain ops
# each, so "ops" in terrain_probe grows by 12) and the client copy still matches the server
# (terrain_clhash). Grapple (impulse 71): aimed up and held, the player is pulled toward the
# ceiling, so the origin z in the PHASE grapple terrain_probe line is well above PHASE start.
# Screenshots show the platforms and the light string.
set pagination off
set confirm off
set debuginfod enabled off
handle SIG32 SIG33 SIG34 SIG35 SIGPIPE SIGUSR1 nostop noprint pass
break SCR_UpdateScreen
run
ignore 1 300
continue
call (void)Cbuf_AddText("terrain_crawlers 0; map drgbox\n")
ignore 1 40
continue
call (void)Cbuf_AddText("god; notarget\n")
ignore 1 360
continue
call (void)Cbuf_AddText("impulse 70; echo PHASE start; terrain_probe; +lookdown\n")
ignore 1 40
continue
call (void)Cbuf_AddText("-lookdown; +attack\n")
ignore 1 600
continue
call (void)Cbuf_AddText("-attack\n")
ignore 1 60
continue
call (void)Cbuf_AddText("echo PHASE platforms; terrain_probe; terrain_clhash; screenshot\n")
ignore 1 10
continue
call (void)Cbuf_AddText("impulse 71; +lookup\n")
ignore 1 80
continue
call (void)Cbuf_AddText("-lookup; +attack\n")
ignore 1 60
continue
call (void)Cbuf_AddText("screenshot\n")
ignore 1 200
continue
call (void)Cbuf_AddText("echo PHASE grapple; terrain_probe; -attack; impulse 2\n")
ignore 1 20
continue
call (void)Cbuf_AddText("condump\n")
ignore 1 5
continue
call (void)Cbuf_AddText("quit\n")
ignore 1 5
continue
