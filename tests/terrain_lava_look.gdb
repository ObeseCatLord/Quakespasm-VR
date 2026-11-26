# Particle lava look round. Run: tests/run-headless.sh tests/terrain_lava_look.gdb
# One fixed scene at a fixed camera (cave seed 1): an older pool in the basin ahead that has cooled and
# crusted, a stream running down the slope, a spray against the wall. The server is paused, then the same
# frame is screenshot once per r_terrain_lava_style (blobs, goo, chunky, crust). Two more shots are taken
# live while lava melts a wall (steam). Last, a pool of 4096 settled particles: the simulation cost
# with sleeping on and off, and the vertex-fill cost and GPU frame time of each style.
# Screenshots come out in this order: blobs goo chunky crust, steam1 steam2.
# Read "LOOK_" and terrain_lava_info lines in condump.txt.
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
define frames
  ignore 1 $arg0
  continue
end

cmd "god; r_drawentities 0; r_drawviewmodel 0; con_notifytime 0; scr_speeds 1; terrain_lava_clear; echo LOOK_SCENE\n"
cmd "terrain_lava_melt 0; terrain_lava_cooling 0.04; terrain_lava_crust 0.06; setpos -120 -20 -150 25 0 0\n"
set $t0 = sv.qcvm.time
cmd "terrain_lava_emit 170 -30 -200 0 0 -20 400 4\n"
waitsim 14.0
cmd "terrain_lava_emit 60 0 -150 0 0 -30 150 3; terrain_lava_emit 40 30 -190 500 300 150 160 2 0.04\n"
waitsim 17.0
cmd "pause; echo LOOK_PAUSED; terrain_lava_info\n"
frames 20
cmd "r_terrain_lava_style 0; terrain_lava_style\n"
frames 10
cmd "screenshot\n"
cmd "r_terrain_lava_style 1; terrain_lava_style\n"
frames 10
cmd "screenshot\n"
cmd "r_terrain_lava_style 2; terrain_lava_style\n"
frames 10
cmd "screenshot\n"
cmd "r_terrain_lava_style 3; terrain_lava_style\n"
frames 10
cmd "screenshot\n"
cmd "pause\n"

# steam: a stream held on the wall, melting, with steam on
cmd "echo LOOK_STEAM; terrain_lava_clear; terrain_lava_melt 1; terrain_lava_cooling 0.02; terrain_lava_crust 0.03; r_terrain_lava_style 0; setpos 0 0 -200 0 90 0\n"
set $t0 = sv.qcvm.time
cmd "+terrain_lava\n"
waitsim 2.2
cmd "screenshot\n"
waitsim 3.2
cmd "-terrain_lava; screenshot; terrain_lava_info\n"
waitsim 4.2
cmd "screenshot\n"

# cost: 4096 settled particles
cmd "echo LOOK_COST; terrain_lava_clear; terrain_lava_melt 0; terrain_lava_cooling 0.0; terrain_lava_crust 0; terrain_lava_cap 4096; terrain_lava_sleep 1; setpos -120 -20 -150 25 0 0\n"
cmd "terrain_lava_block 4096 170 -30 -200\n"
set $t0 = sv.qcvm.time
waitsim 24.0
cmd "echo LOOK_COST_SLEEP_ON; terrain_lava_info\n"
frames 60
cmd "r_terrain_lava_style 0; echo LOOK_GPU_blobs\n"
frames 90
cmd "terrain_lava_style info\n"
cmd "r_terrain_lava_style 1; echo LOOK_GPU_goo\n"
frames 90
cmd "terrain_lava_style info\n"
cmd "r_terrain_lava_style 2; echo LOOK_GPU_chunky\n"
frames 90
cmd "terrain_lava_style info\n"
cmd "r_terrain_lava_style 3; echo LOOK_GPU_crust\n"
frames 90
cmd "terrain_lava_style info\n"
cmd "terrain_lava_draw 0; echo LOOK_GPU_nolava\n"
frames 90
cmd "terrain_lava_style info\n"
cmd "terrain_lava_draw 1; terrain_lava_clear; terrain_lava_sleep 0\n"
cmd "terrain_lava_block 4096 170 -30 -200\n"
set $t0 = sv.qcvm.time
waitsim 24.0
cmd "echo LOOK_COST_SLEEP_OFF; terrain_lava_info; condump\n"
cmd "quit\n"
ignore 1 30
continue
