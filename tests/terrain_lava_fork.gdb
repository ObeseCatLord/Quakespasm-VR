# Particle lava prototype, part 3: does the stream follow a channel that is dug? Run:
#   tests/run-headless.sh tests/terrain_lava_fork.gdb
# Three runs of the same emitter above the slope ahead of the start (the map is reloaded between
# them so the terrain is fresh): no channel, a channel dug to one side (terrain_lava_trench, ordinary
# dig ops), a channel dug to the other. Each prints the centroid of the settled particles after 8 s.
# Expect the three centroids to differ in heading, toward the dug side when the channel runs downhill.
set pagination off
set confirm off
set debuginfod enabled off
handle SIG32 SIG33 SIG34 SIG35 SIGPIPE SIGUSR1 nostop noprint pass
break SCR_UpdateScreen
run
ignore 1 300
continue
call (void)Cbuf_AddText("terrain_crawlers 0; terrain_lava_melt 0; terrain_lava_cooling 0.005; terrain_lava_contact_cooling 0.02; map drgbox\n")
ignore 1 500
continue

define waitsim
  condition 1 sv.qcvm.time >= $t0 + $arg0
  continue
  condition 1
end
define cmd
  call (void)Cbuf_AddText($arg0)
  ignore 1 3
  continue
end
define run_stream
  cmd "terrain_lava_clear; setpos 0 0 -200 20 0 0\n"
  set $t0 = sv.qcvm.time
  cmd "terrain_lava_emit 60 0 -150 0 0 -30 150 2\n"
  waitsim 4.0
  cmd "echo FORK_T4; terrain_lava_info\n"
  waitsim 8.0
  cmd "screenshot; echo FORK_T8; terrain_lava_info\n"
end

cmd "god; echo FORK_BASELINE\n"
run_stream
cmd "map drgbox\n"
ignore 1 400
continue
cmd "god; echo FORK_A; terrain_lava_trench 62 -2 -200 0.99 0.16 110 20\n"
run_stream
cmd "map drgbox\n"
ignore 1 400
continue
cmd "god; echo FORK_B; terrain_lava_trench 62 -2 -200 -0.36 -0.93 110 20\n"
run_stream
cmd "condump\n"
cmd "quit\n"
ignore 1 30
continue
