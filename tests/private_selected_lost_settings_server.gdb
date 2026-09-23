# Selected-private loopback probe: change gravity after completed move 100,
# then suppress exactly the first nonempty unreliable datagram to client 0.
# Requires the debug-symbol dedicated server and the selected private client.
set pagination off
set confirm off
set debuginfod enabled off
set breakpoint pending on

break SV_Physics if svs.clients[0].private_pmove_walk_selected && svs.clients[0].private_completed_move > 100 && sv_gravity.value > 799
commands
 silent
 call Cvar_SetQuick(&sv_gravity, "600")
 printf "QSVR_GRAVITY_CHANGED value=%g completed=%d\n", sv_gravity.value, svs.clients[0].private_completed_move
 disable 1
 enable 2
 continue
end

# This breakpoint is armed only by the gravity-change breakpoint above. Its
# one-shot return skips the network driver's send while reporting success.
break NET_SendUnreliableMessage if sock == svs.clients[0].netconnection && svs.clients[0].private_pmove_walk_selected && data->cursize > 0
commands
 silent
 disable 2
 printf "QSVR_DROPPED_FIRST_UNRELIABLE size=%d gravity=%g completed=%d\n", data->cursize, sv_gravity.value, svs.clients[0].private_completed_move
 return (int) 0
 continue
end
disable 2
run
