# Netsync of the cave. Run: tests/run-headless.sh tests/terrain_netsync.gdb
# The listen server's own client draws its own copy of the cave, built from the seed and the op
# stream the server sends it. After digs, fills and an explosion, "terrain_clhash" must print the
# same hash for the client copy as for the server's region, with the client's op count equal to
# the server's (terrain_probe prints that as "ops N"). The screenshot shows the client copy drawn.
set pagination off
set confirm off
set debuginfod enabled off
handle SIG32 SIG33 SIG34 SIG35 SIGPIPE SIGUSR1 nostop noprint pass
break SCR_UpdateScreen
run
ignore 1 300
continue
call (void)Cbuf_AddText("terrain_crawlers 0; map drgbox\n")
ignore 1 400
continue
call (void)Cbuf_AddText("god; notarget; echo PHASE start; terrain_clhash; terrain_probe\n")
ignore 1 20
continue
call (void)Cbuf_AddText("terrain_dig; wait; terrain_dig; wait; terrain_fill; wait; terrain_dig\n")
ignore 1 60
continue
call (void)Cbuf_AddText("impulse 7; give r 20\n")
ignore 1 30
continue
call (void)Cbuf_AddText("+attack\n")
ignore 1 20
continue
call (void)Cbuf_AddText("-attack\n")
ignore 1 200
continue
call (void)Cbuf_AddText("echo PHASE edited; terrain_clhash; terrain_probe; screenshot\n")
ignore 1 10
continue
call (void)Cbuf_AddText("condump\n")
ignore 1 5
continue
call (void)Cbuf_AddText("quit\n")
ignore 1 5
continue
