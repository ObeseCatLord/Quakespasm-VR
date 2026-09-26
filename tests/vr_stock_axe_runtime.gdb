# Headless stock id1 physical axe regression using installed pak assets.
# The two queued commands exercise the ordinary server physics owner.
set pagination off
set confirm off
set debuginfod enabled off
set print thread-events off
break SV_Physics
run
python
import gdb

def run(command):
    try:
        gdb.execute(command, to_string=True)
    except gdb.error:
        print('STOCK_AXE_GDB_COMMAND_FAILED: %s' % command)
        print(gdb.execute('bt 12', to_string=True))
        raise

def integer(expr):
    return int(gdb.parse_and_eval(expr))

def number(expr):
    return float(gdb.parse_and_eval(expr))

def vector(expr, values):
    for axis, value in enumerate(values):
        run('set %s[%d] = %.9g' % (expr, axis, value))

assert integer('SV_VRStockAxeContactProfile()') == 1, \
    'installed id1 program failed the exact stock axe descriptor gate'
assert not integer('SV_VRStockAxeMeleeEnabled()'), \
    'stock melee policy unexpectedly enabled by default'
run('call (void)Cvar_SetQuick(&sv_immersive_melee,"1")')
assert integer('SV_VRStockAxeMeleeEnabled()'), \
    'explicit stock melee policy did not enable the pinned program'

run('set $client = &svs.clients[0]')
run('set $p = EDICT_NUM(1)')
for field, value in (('active', 1), ('spawned', 1), ('knowntoqc', 1),
                     ('protocol_qsvr', 1)):
    run('set $client->%s = %d' % (field, value))
run('set $client->edict = $p')
run('set $client->message.data = $client->msgbuf')
run('set $client->message.maxsize = sizeof($client->msgbuf)')
run('set $client->message.cursize = 0')
run('set pr_global_struct->self = (int)((char *)$p - (char *)qcvm->edicts)')
run('set $put = ED_FindFunction("PutClientInServer")')
assert integer('$put != 0')
run('call (void)PR_ExecuteProgram($put - qcvm->functions)')
run('set $stand = ED_FindFunction("player_stand1")')
run('set $running = ED_FindFunction("player_run")')
assert integer('$stand != 0 && $running != 0')
run('set $p->v.weapon = 4096')
run('set $p->v.weaponmodel = PR_SetEngineString("progs/v_axe.mdl")')
run('set $p->v.think = $stand - qcvm->functions')
run('set $p->v.nextthink = qcvm->time + 1')
run('set $p->v.health = 100')
run('set $p->v.deadflag = 0')
vector('$p->v.view_ofs', (0, 0, 22))
run('set $cooldown = GetEdictFieldValueByName($p,"attack_finished")')
assert integer('$cooldown != 0')
run('set $cooldown->_float = qcvm->time')
if number('realtime') <= 0:
    run('set realtime = 1')
run('set $client->lastmovetime = realtime')
run('set $client->cmd.msec = 50')
run('set $client->cmd.seconds = 0.05')
run('set $client->cmd.buttons = 1')
run('set $client->cmd.vr_active = 1')
run('set $client->cmd.vr_handpos_relative = 1')
run('set $client->cmd.vr_akimbo_active = 0')
run('set $client->cmd.vr_akimbo_berserk = 0')
run('set $client->cmd.vr_contact_received = realtime')
run('set $client->cmd.vr_contact.flags = 5')  # left + immersive melee
run('set $client->cmd.vr_contact.weapon = 4096')
run('set $client->cmd.vr_contact.modelindex = SV_ModelIndex("progs/v_axe.mdl")')
assert integer('$client->cmd.vr_contact.modelindex > 0')
vector('$client->cmd.vr_handrot', (0, 0, 0))
vector('$client->cmd.vr_contact.grip[0]', (8, 0, 22))
run('set $client->cmd.vr_contact.speed[0] = 2')

origin = [number('$p->v.origin[%d]' % axis) for axis in range(3)]
run('set $target = ED_Alloc()')
run('set $target->v.health = 1000')
run('set $target->v.takedamage = 1')
run('set $target->v.solid = 2')
run('set $target->v.classname = PR_SetEngineString("monster_ogre")')
run('set $noop = ED_FindFunction("SUB_Null")')
assert integer('$noop != 0')
for field in ('th_pain', 'th_die'):
    run('set $callback = GetEdictFieldValueByName($target,"%s")' % field)
    assert integer('$callback != 0'), field
    run('set $callback->function = $noop - qcvm->functions')
vector('$target->v.origin', (origin[0] + 36, origin[1], origin[2] + 22))
vector('$target->v.mins', (-4, -4, -4))
vector('$target->v.maxs', (4, 4, 4))
vector('$target->v.size', (8, 8, 8))
run('call (void)SV_LinkEdict($target,0)')

def sample(sequence, x):
    run('set $client->cmd.sequence = %d' % sequence)
    for endpoint, z in (('base', 20), ('tip', 24)):
        vector('$client->cmd.vr_contact.%s[0]' % endpoint, (x, 0, z))

# Ten units of point travel in 50 ms at declared speed 2 stays inside
# the server transition cap; the target begins at the crossed edge.
sample(1, 24)
run('set $client->private_cmd_queue[0] = $client->cmd')
sample(2, 34)
run('set $client->private_cmd_queue[1] = $client->cmd')
run('set $client->private_cmd_queue_head = 0')
run('set $client->private_cmd_queue_count = 2')
run('set $client->private_cmd_queue_msec = 100')
run('set $client->lastmovemessage = 2')
run('set $client->private_vr_contact_spawn_seen = 1')
run('set $client->private_vr_contact_cursor_valid = 1')
run('set $client->private_vr_contact_last_sequence = 0')
run('set $p->v.button0 = 1')
run('set host_frametime = 0')
run('set pr_global_struct->frametime = 0')
run('set host_client = $client')
run('set sv_player = $p')
assert integer('SV_VRMeleeSuppressNativeTrigger($client,$p,&$client->cmd)'), \
    'fresh queued stock contact did not suppress the held trigger'
run('call (void)SV_Physics_Client($p,1)')
health = number('$target->v.health')
assert 0 < health < 1000, 'linked edge target took no physical axe damage'
physical_cooldown = number('$cooldown->_float')
assert physical_cooldown > number('(float)qcvm->time'), \
    'physical hit did not advance the native QC attack cooldown'
assert integer('$client->private_vr_melee_consumed[0]'), \
    'physical axe stroke was not consumed'
assert integer('$client->private_vr_contact_previous_valid'), \
    'ordinary physics lost accepted stock contact continuity'
assert integer('SV_VRMeleeSuppressNativeTrigger($client,$p,&$client->cmd)'), \
    'accepted stock contact lost held-trigger suppression'
assert integer('$p->v.think') in (integer('$stand - qcvm->functions'),
                                integer('$running - qcvm->functions')), \
    'held VR trigger scheduled a native axe attack'

# Make the native QC attack eligible; otherwise its own cooldown would hide a
# suppression regression in the next ordinary physics tick.
run('set $cooldown->_float = (float)qcvm->time')
run('call (void)SV_Physics_Client($p,1)')
assert number('$target->v.health') == health, 'held VR trigger duplicated damage'
assert integer('$p->v.think') in (integer('$stand - qcvm->functions'),
                                integer('$running - qcvm->functions')), \
    'held VR trigger scheduled a native axe attack on the next tick'

run('set $client->private_cmd_queue_count = 0')
run('set $client->private_cmd_queue_msec = 0')
run('call (void)SV_ResetPrivateVRContactState($client)')
run('set $client->cmd.vr_active = 0')
run('set $p->v.think = $stand - qcvm->functions')
run('set $p->v.nextthink = qcvm->time + 1')
run('set $cooldown->_float = qcvm->time')
run('set $p->v.button0 = 1')
assert not integer('SV_VRMeleeSuppressNativeTrigger($client,$p,&$client->cmd)'), \
    'desktop command was still suppressed'
run('call (void)SV_Physics_Client($p,1)')
assert integer('$p->v.think') not in (integer('$stand - qcvm->functions'),
                                    integer('$running - qcvm->functions')), \
    'desktop native axe attack did not schedule'
print('VR_STOCK_AXE_RUNTIME_PASSED target_health=%g physical_cooldown=%g' %
      (health, physical_cooldown))
end
quit 0
