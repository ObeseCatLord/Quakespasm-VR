# Isolated stock-map production dispatcher proof. Loop_Init is enabled only
# for headless initialization without UDP; no packets or connected-peer proof.
set pagination off
set confirm off
set debuginfod enabled off
set print thread-events off
break Loop_Init
commands
 silent
 return 0
 continue
end
break SV_Physics
run
python
import gdb

def execute(command):
    return gdb.execute(command, to_string=True)

def integer(expression):
    return int(gdb.parse_and_eval(expression))

execute('set $owner = EDICT_NUM(1)')
execute('set $null = (func_t)(ED_FindFunction("SUB_Null") - qcvm->functions)')
execute('set $remove = (func_t)(ED_FindFunction("SUB_Remove") - qcvm->functions)')
assert integer('$null') > 0 and integer('$remove') > 0
execute('set host_client = &svs.clients[0]')
execute('set sv_player = $owner')
execute('set host_client->edict = $owner')
execute('set host_client->active = 1')
execute('set host_client->spawned = 1')
execute('set host_client->knowntoqc = 1')
execute('set pr_global_struct->PlayerPreThink = $null')
execute('set pr_global_struct->PlayerPostThink = $null')
# Stock QC has no customphysics field. Alias the fixture callback to think;
# the same native extension lookup resolves the real slot from a mod progs.
execute('set qcvm->extfields.customphysics = ED_FindFieldOffset("think")')
assert integer('qcvm->extfields.customphysics') >= 0
execute('set $owner->free = 0')
execute('set $owner->v.movetype = 999')
execute('set $owner->v.solid = 0')
execute('set $owner->v.health = 100')
execute('set $owner->v.think = $null')
execute('set $owner->v.nextthink = qcvm->time + 0.001')
execute('set $scheduled = $owner->v.nextthink')
execute('set $owner->v.origin[0] = 37')
execute('set $owner->v.origin[1] = 11')
execute('set $owner->v.origin[2] = 24')
execute('set $owner->v.velocity[0] = 100')
execute('set host_frametime = 0.02')
execute('set $completed = SV_Physics_ClientNativeFromPhase($owner, 1, 41, 0, 0, 0, 0)')
assert integer('$completed') == 1
assert integer('$owner->v.movetype') == 999  # native dispatch would error
assert float(gdb.parse_and_eval('$owner->v.nextthink')) == float(gdb.parse_and_eval('$scheduled'))
assert float(gdb.parse_and_eval('$owner->v.origin[0]')) == 37
assert integer('$owner->retain_count') == 0

# A custom callback may remove its owner: suppress PostThink and completion.
execute('set $owner->v.think = $remove')
execute('set pr_global_struct->PlayerPostThink = $remove')
execute('set $completed = SV_Physics_ClientNativeFromPhase($owner, 1, 42, 0, 0, 0, 0)')
assert integer('$completed') == 0 and integer('$owner->free') == 1
assert integer('$owner->retain_count') == 0
gdb.write('CUSTOMPHYSICS_NATIVE_PASSED\n')
end
quit 0
