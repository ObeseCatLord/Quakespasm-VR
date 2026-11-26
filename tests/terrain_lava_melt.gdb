# Particle lava prototype, part 2: melting. Run: tests/run-headless.sh tests/terrain_lava_melt.gdb
# The player stands at the cave start looking down the slope ahead (god). The weapon sprays for 3 s,
# then everything is left to cool. The script prints terrain_lava_info once per second of server time.
# Expect: ops grow by at most terrain_lava_ops_rate (4) per second plus a burst of one or two;
# the "first dig" line reads rock (negative) before and air (positive) just after the op; after
# the last particle has frozen or been removed (live 0) the op count no longer changes. Screenshots
# show the spray on the surface with the lights, then the pit and channel it left.
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

cmd "god; terrain_lava_clear; setpos 0 0 -200 20 0 0; echo MELT_START\n"
set $t0 = sv.qcvm.time
cmd "+terrain_lava\n"
waitsim 1.5
cmd "screenshot\n"
waitsim 3.0
cmd "-terrain_lava; screenshot\n"
set $i = 4
while $i <= 40
  waitsim $i
  call (void)Cbuf_AddText("echo MELT_TICK; terrain_lava_info\n")
  ignore 1 2
  continue
  set $i = $i + 1
end
cmd "screenshot; terrain_lava_emit 0 0 -150 0 0 -50 1 0.1; echo MELT_END; terrain_lava_info; condump\n"
cmd "quit\n"
ignore 1 30
continue
