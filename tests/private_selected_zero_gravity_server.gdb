# Diagnostic server wrapper for a live selected-private zero-valued setting.
# Start a fresh selected e1m1 dedicated server and run the loopback client with
# QSVR_LOCAL_ASSERT_MOVE_STATS=1 QSVR_LOCAL_EXPECT_GRAVITY=0 plus the coherent
# owner and action-ACK checks. This changes gravity once before a physics step;
# it does not modify production server policy. Use a fresh server; the
# breakpoint deliberately matches the default gravity before changing it.
set pagination off
set confirm off
set debuginfod enabled off
set breakpoint pending on
break SV_Physics if svs.clients[0].private_pmove_walk_selected && svs.clients[0].private_completed_move > 100 && sv_gravity.value > 799
commands
 silent
 call Cvar_SetQuick(&sv_gravity, "0")
 printf "QSVR_GRAVITY_CHANGED value=%g completed=%d\n", sv_gravity.value, svs.clients[0].private_completed_move
 disable 1
 continue
end
run
