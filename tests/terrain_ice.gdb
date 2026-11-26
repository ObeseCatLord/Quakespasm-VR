# Falling ice. Run: tests/run-headless.sh tests/terrain_ice.gdb   (Vulkan ICD set up first)
# Phase A: build a pillar over a monster, cut its root, watch it fall, land, rest and be stamped back;
#          terrain_ice_info prints CHECK lines (PASS/FAIL); terrain_ice_verify replays the op log.
# Phase B: a second pillar over a ramp, tossed sideways, to see tumbling and sliding.
# Phase C: the cost with 1, 4 and 16 bodies.
set pagination off
set confirm off
set debuginfod enabled off
handle SIG32 SIG33 SIG34 SIG35 SIGPIPE SIGUSR1 nostop noprint pass
break SCR_UpdateScreen
run
ignore 1 300
continue
call (void)Cbuf_AddText("terrain_crawlers 0; terrain_worm_auto 0; map drgbox\n")
ignore 1 500
continue
call (void)Cbuf_AddText("god; echo PHASE A; setpos 0 0 -190 -75 0 0\n")
ignore 1 20
continue
call (void)Cbuf_AddText("terrain_ice_pillar 280; terrain_ice_dummy; terrain_ice_info\n")
ignore 1 40
continue
call (void)Cbuf_AddText("terrain_ice_cam pillar 340; screenshot; echo BEFORE_CUT\n")
ignore 1 20
continue
call (void)Cbuf_AddText("terrain_ice_cut\n")
ignore 1 8
continue
call (void)Cbuf_AddText("terrain_ice_cam body 300; screenshot; echo MIDFALL; terrain_ice_info\n")
ignore 1 14
continue
call (void)Cbuf_AddText("terrain_ice_cam body 260; screenshot; echo AFTERLAND; terrain_ice_info\n")
ignore 1 60
continue
call (void)Cbuf_AddText("echo SETTLING; terrain_ice_info\n")
ignore 1 200
continue
call (void)Cbuf_AddText("echo AT_REST; terrain_ice_cam rest 260; screenshot; terrain_ice_info; terrain_ice_verify\n")
ignore 1 40
continue
call (void)Cbuf_AddText("echo PHASE B; setpos 400 -100 -190 -75 0 0\n")
ignore 1 20
continue
call (void)Cbuf_AddText("terrain_ice_pillar 300; terrain_ice_ramp; terrain_ice_info\n")
ignore 1 40
continue
call (void)Cbuf_AddText("terrain_ice_cam pillar 340; screenshot\n")
ignore 1 20
continue
call (void)Cbuf_AddText("terrain_ice_cut\n")
ignore 1 10
continue
call (void)Cbuf_AddText("terrain_ice_toss 120 0 0 0 4 0; echo B_TOSSED\n")
ignore 1 12
continue
call (void)Cbuf_AddText("terrain_ice_cam body 300; screenshot; terrain_ice_info\n")
ignore 1 12
continue
call (void)Cbuf_AddText("terrain_ice_cam body 300; screenshot; terrain_ice_info\n")
ignore 1 12
continue
call (void)Cbuf_AddText("terrain_ice_cam body 300; screenshot; terrain_ice_info\n")
ignore 1 25
continue
call (void)Cbuf_AddText("terrain_ice_info\n")
ignore 1 25
continue
call (void)Cbuf_AddText("terrain_ice_info\n")
ignore 1 250
continue
call (void)Cbuf_AddText("echo B_REST; terrain_ice_cam rest 260; screenshot; terrain_ice_info; terrain_ice_verify\n")
ignore 1 40
continue
call (void)Cbuf_AddText("echo PHASE C; terrain_ice_bench 1 600; terrain_ice_bench 4 600; terrain_ice_bench 16 600\n")
ignore 1 60
continue
call (void)Cbuf_AddText("echo DONE; terrain_ice_info; condump\n")
ignore 1 10
continue
call (void)Cbuf_AddText("quit\n")
ignore 1 5
continue
