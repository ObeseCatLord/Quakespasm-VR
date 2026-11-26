# The lava sprayer tool. Run: tests/run-headless.sh tests/workshop_lava_tool.gdb
# Read "LT_" lines and workshop_info / terrain_lava_info / terrain_clhash in condump.txt.
# Phases (each prints LT_<name> before its numbers):
#   LT_SELECT   impulse 3 (super shotgun) then impulse 72: tool lavagun, lava 100, shells unchanged later
#   LT_SPRAY    attack held 3 s facing the ice wall: "emitted" grows, lava < 100, dig ops grow, shells unchanged
#   LT_REFILL   attack released 1 s delay + 5 s: lava is higher than at the end of LT_SPRAY
#   LT_DRY      a fast drain (60 per second): tank runs dry ("dry"), emitted stops growing while the button
#               stays held, then refills past workshop_lava_restart and sprays again
#   LT_WHEEL    impulse 10 from the platform gun goes to the lava sprayer, once more to Quake's first weapon
#   LT_END      terrain_clhash: client hash equals server hash, same ops count
# Screenshots: 1 the sprayer idle in hand, 2 the platform gun and 3 the grapple for comparison, 4 spraying,
# 5 later (steam where the lava melts the wall).
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

cmd "god; notarget; con_notifytime 0; terrain_lava_clear; echo LT_SELECT; impulse 9\n"
frames 10
cmd "impulse 3\n"
frames 10
cmd "setpos 0 0 -200 0 90 0\n"
frames 10
cmd "workshop_info; impulse 72\n"
frames 10
cmd "workshop_info; terrain_lava_info; terrain_clhash; screenshot\n"
frames 10
cmd "echo LT_COMPARE; impulse 70\n"
frames 12
cmd "screenshot\n"
cmd "impulse 71\n"
frames 12
cmd "screenshot\n"
cmd "impulse 72\n"
frames 12
cmd "echo LT_SPRAY; +attack\n"
set $t0 = sv.qcvm.time
waitsim 1.2
cmd "screenshot\n"
waitsim 3.0
cmd "workshop_info; terrain_lava_info\n"
waitsim 4.6
cmd "screenshot; -attack; echo LT_RELEASED; workshop_info; terrain_lava_info\n"
set $t0 = sv.qcvm.time
waitsim 6.0
cmd "echo LT_REFILL; workshop_info\n"
cmd "echo LT_DRY; workshop_lava_drain 60; terrain_lava_clear; +attack\n"
set $t0 = sv.qcvm.time
waitsim 3.0
cmd "workshop_info; terrain_lava_info\n"
waitsim 4.0
cmd "echo LT_DRY_LATER; workshop_info; terrain_lava_info\n"
cmd "-attack\n"
set $t0 = sv.qcvm.time
waitsim 7.0
cmd "echo LT_DRY_REFILLED; workshop_info; +attack\n"
set $t0 = sv.qcvm.time
waitsim 0.6
cmd "workshop_info; terrain_lava_info; -attack\n"
cmd "workshop_lava_drain 12; echo LT_WHEEL; impulse 70\n"
frames 10
cmd "workshop_info; impulse 10\n"
frames 10
cmd "workshop_info; impulse 10\n"
frames 10
cmd "workshop_info\n"
set $t0 = sv.qcvm.time
cmd "impulse 72; setpos 0 0 -200 0 90 0; +attack\n"
waitsim 2.0
cmd "-attack; echo LT_END; terrain_probe; terrain_clhash; terrain_lava_info; condump\n"
cmd "quit\n"
ignore 1 30
continue
