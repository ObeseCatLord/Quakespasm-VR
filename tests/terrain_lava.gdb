# Particle lava prototype, part 1. Run: tests/run-headless.sh tests/terrain_lava.gdb
# The player stands still (god, noclip via setpos) at the cave start; the cave is seed 1.
# Phases (sim time is server time, so the checks do not depend on the frame rate):
#   A  hold +terrain_lava and look in four directions, screenshot each (spray on walls, lights)
#   B  pool: 1600 particles dropped into the basin ahead; after settling print info
#   C  slope: a stream above the slope ahead; centroid at 3 s and at 7 s (z must fall)
#   D  cost: info at about 2k, 4k and 8k live particles
# Read the "terrain_lava_info" lines in condump.txt. Expect after B: maxspeed < 40, inside < 0.01,
# meanneigh between 8 and 45; after C: centroid z lower at the second reading; D: cost_ms per step.
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

cmd "god; terrain_lava_clear; echo PHASE_A\n"
set $t0 = sv.qcvm.time
cmd "setpos 0 0 -200 20 0 0; +terrain_lava\n"
waitsim 2.0
cmd "screenshot; terrain_lava_info\n"
cmd "setpos 0 0 -200 0 90 0\n"
waitsim 4.0
cmd "screenshot\n"
cmd "setpos 0 0 -200 0 180 0\n"
waitsim 6.0
cmd "screenshot\n"
cmd "setpos 0 0 -200 0 270 0\n"
waitsim 8.0
cmd "screenshot; -terrain_lava; terrain_lava_info\n"

cmd "echo PHASE_B; terrain_lava_clear; terrain_lava_melt 0; terrain_lava_cooling 0.005; terrain_lava_contact_cooling 0.02; setpos 0 0 -200 20 0 0\n"
set $t0 = sv.qcvm.time
cmd "terrain_lava_emit 170 -30 -200 0 0 -20 400 4\n"
waitsim 5.0
cmd "screenshot; echo POOL_T5; terrain_lava_info\n"
waitsim 10.0
cmd "screenshot; echo POOL_T10; terrain_lava_info\n"
waitsim 12.0
cmd "echo POOL_T12; terrain_lava_info\n"

cmd "echo PHASE_C; terrain_lava_clear; setpos 0 0 -200 20 0 0\n"
set $t0 = sv.qcvm.time
cmd "terrain_lava_emit 60 0 -150 0 0 -30 150 2\n"
waitsim 3.0
cmd "echo SLOPE_T3; terrain_lava_info\n"
waitsim 7.0
cmd "screenshot; echo SLOPE_T7; terrain_lava_info\n"

cmd "echo PHASE_D; terrain_lava_clear; terrain_lava_cap 2048; setpos 0 0 -200 20 0 0\n"
cmd "terrain_lava_block 2048 40 0 -150\n"
set $t0 = sv.qcvm.time
waitsim 3.0
cmd "echo COST_2K; terrain_lava_info\n"
cmd "terrain_lava_clear; terrain_lava_cap 4096\n"
cmd "terrain_lava_block 4096 40 0 -150\n"
set $t0 = sv.qcvm.time
waitsim 3.0
cmd "echo COST_4K; terrain_lava_info\n"
cmd "terrain_lava_clear; terrain_lava_cap 8192\n"
cmd "terrain_lava_block 8192 40 0 -150\n"
set $t0 = sv.qcvm.time
waitsim 3.0
cmd "screenshot; echo COST_8K; terrain_lava_info; condump\n"
cmd "quit\n"
ignore 1 30
continue
