# Ice worm prototype. Run: tests/run-headless.sh tests/terrain_worm.gdb   (. a Vulkan ICD setup first)
# The player stands still in god mode. Phases:
#   A  spawn a worm, force three hunts; screenshot the first frames of each burst (the third with
#      extra ambient light); print terrain_worm_info (bursts, ops, spacing error, tunnel check).
#   B  terrain_worm_hurt kills a body segment, then the head: the chain closes up, then the worm dies.
#   C  a fresh worm; one real-weapon attempt (rockets) while it is in the air.
# Expect: bursts >= 2, ops in 1..400, spacing error < 6, tunnel stamp read rock before and air now,
# no server error after the kills.
set pagination off
set confirm off
set debuginfod enabled off
handle SIG32 SIG33 SIG34 SIG35 SIGPIPE SIGUSR1 nostop noprint pass
break SCR_UpdateScreen
run
ignore 1 300
continue
call (void)Cbuf_AddText("terrain_crawlers 0; terrain_worm_auto 0; map drgbox\n")
ignore 1 40
continue
# god as soon as the map is up: in the map-sized cave a monster can reach the start within seconds
call (void)Cbuf_AddText("god\n")
ignore 1 460
continue
call (void)Cbuf_AddText("god; echo PHASE A0; terrain_worm_spawn 600; terrain_probe; terrain_worm_info\n")
ignore 1 400
continue
call (void)Cbuf_AddText("echo PHASE A1; terrain_worm_info; terrain_worm_hunt\n")

define burst
  condition 1 'sv_terrain_worm.c'::worm.state == 1
  continue
  condition 1
  ignore 1 4
  continue
  call (void)Cbuf_AddText("terrain_worm_aim 0; screenshot; echo BURST; terrain_worm_info\n")
  ignore 1 20
  continue
  call (void)Cbuf_AddText("terrain_worm_aim 0; screenshot\n")
  condition 1 'sv_terrain_worm.c'::worm.state == 0
  continue
  condition 1
  call (void)Cbuf_AddText("echo LANDED; terrain_worm_info; terrain_probe\n")
  ignore 1 300
  continue
end

burst
call (void)Cbuf_AddText("terrain_worm_hunt\n")
burst
call (void)Cbuf_AddText("r_terrain_ambient 0.5; terrain_worm_hunt\n")
burst
call (void)Cbuf_AddText("echo PHASE B; terrain_worm_hurt 3 500 qc; terrain_worm_hurt 5 500\n")
ignore 1 30
continue
call (void)Cbuf_AddText("terrain_worm_info; terrain_worm_hurt 0 500 qc\n")
ignore 1 30
continue
call (void)Cbuf_AddText("echo AFTER_HEAD_KILL; terrain_worm_info; terrain_probe\n")
ignore 1 30
continue
call (void)Cbuf_AddText("echo PHASE C; r_terrain_ambient 0.1; terrain_worm_spawn 600\n")
ignore 1 300
continue
call (void)Cbuf_AddText("impulse 9\n")
ignore 1 10
continue
call (void)Cbuf_AddText("impulse 7\n")
ignore 1 10
continue
call (void)Cbuf_AddText("terrain_worm_hunt\n")
ignore 1 10
continue
condition 1 'sv_terrain_worm.c'::worm.state == 1
continue
condition 1
call (void)Cbuf_AddText("terrain_worm_freeze 1; terrain_worm_aim 0; +attack; echo FIRING; terrain_worm_info\n")
ignore 1 40
continue
call (void)Cbuf_AddText("terrain_worm_aim 0; screenshot\n")
ignore 1 450
continue
call (void)Cbuf_AddText("-attack; screenshot\n")
ignore 1 20
continue
call (void)Cbuf_AddText("echo AFTER_WEAPON; terrain_worm_info; terrain_probe\n")
ignore 1 10
continue
call (void)Cbuf_AddText("condump\n")
ignore 1 5
continue
call (void)Cbuf_AddText("quit\n")
ignore 1 5
continue
