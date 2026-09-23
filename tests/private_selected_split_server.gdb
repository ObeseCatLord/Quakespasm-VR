# Diagnostic server wrapper for an opt-in stock-QC private WALK trial.
# Supply normal dedicated arguments and run a separate loopback client.
# Shrink only the selected peer's datagram budget and mark its currently
# visible non-owner entities for reset once; this forces a real continuation
# without changing production server policy. 256 is UF_RESET.
set pagination off
set confirm off
set debuginfod enabled off
set breakpoint pending on
break SV_SendClientDatagram if client->private_pmove_walk_selected && client->limit_unreliable > 220
commands
 silent
 set client->limit_unreliable = 220
 python
import gdb
client = gdb.parse_and_eval('client')
count = int(client['numpreviousentities'])
forced = 0
for i in range(count):
    entnum = int(client['previousentities'][i]['num'])
    if entnum > 1: # first stock player occupies entity 1
        gdb.execute('set client->pendingentities_bits[%d] = 256' % entnum)
        forced += 1
gdb.write('QSVR_FORCED_PENDING visible=%d forced=%d\n' % (count, forced))
 end
 disable 1
 continue
end
break SVFTE_WriteEntitiesToClient if client->private_pmove_walk_selected && client->snapshotresume > 0
commands
 silent
 printf "QSVR_CONTINUATION_SEEN resume=%d\n", client->snapshotresume
 disable 2
 continue
end
run
