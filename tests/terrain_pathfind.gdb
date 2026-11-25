# Walking routes for vanilla monsters. Run: tests/run-headless.sh tests/terrain_pathfind.gdb
# The player stands still in god mode and every monster is told to hunt them, first with
# terrain_pathfind 1, then on a fresh load with terrain_pathfind 0 (the stock chase). Each
# terrain_ent line ends with "route N": the cost of the monster's route to the player (about 2
# per 32 units walked; -1: no route on foot; -2: not standing on any cell the grid knows, usually
# wedged in a wall). Seen in one run on seed 1 at skill 1 with jumps (terrain_monsterclimb 3): 23
# of 25 monsters start with a route; with routes on, all but two of them reach the player by PHASE
# on1 (most are then killed in the crossfire); with routes off, a few do and most end wedged in a
# wall (overlap 1). Numbers vary between runs.
set pagination off
set confirm off
set debuginfod enabled off
handle SIG32 SIG33 SIG34 SIG35 SIGPIPE SIGUSR1 nostop noprint pass
break SCR_UpdateScreen
run
ignore 1 300
continue
call (void)Cbuf_AddText("terrain_crawlers 0; terrain_pathfind 1; map drgbox\n")
ignore 1 500
continue
call (void)Cbuf_AddText("god; terrain_wake; echo PHASE on0; terrain_ents\n")
ignore 1 1500
continue
call (void)Cbuf_AddText("echo PHASE on1; terrain_ents\n")
ignore 1 1500
continue
call (void)Cbuf_AddText("echo PHASE on2; terrain_ents; screenshot\n")
ignore 1 10
continue
call (void)Cbuf_AddText("terrain_pathfind 0; map drgbox\n")
ignore 1 500
continue
call (void)Cbuf_AddText("god; terrain_wake; echo PHASE off0; terrain_ents\n")
ignore 1 1500
continue
call (void)Cbuf_AddText("echo PHASE off1; terrain_ents\n")
ignore 1 1500
continue
call (void)Cbuf_AddText("echo PHASE off2; terrain_ents\n")
ignore 1 10
continue
call (void)Cbuf_AddText("condump\n")
ignore 1 5
continue
call (void)Cbuf_AddText("quit\n")
ignore 1 5
continue
