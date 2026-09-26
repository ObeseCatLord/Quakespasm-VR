# Headless Dwell contact processing against the installed pinned QuakeC.
# Tests real point sweeps and callback damage; no tracked headset is used.
set pagination off
set confirm off
set debuginfod enabled off
set print thread-events off
break SV_Physics
run
python
import gdb

def run(command):
    gdb.execute(command, to_string=True)

def integer(expr):
    return int(gdb.parse_and_eval(expr))

def number(expr):
    return float(gdb.parse_and_eval(expr))

def vector(expr, values):
    for axis, value in enumerate(values):
        run('set %s[%d] = %.9g' % (expr, axis, value))

run('set $client = &svs.clients[0]')
run('set $p = EDICT_NUM(1)')
run('call (void)Cvar_SetQuick(&sv_immersive_melee, "1")')
for field, value in (('active', 1), ('spawned', 1), ('knowntoqc', 1),
                     ('protocol_qsvr', 1)):
    run('set $client->%s = %d' % (field, value))
run('set $client->edict = $p')
run('set pr_global_struct->self = (int)((char *)$p - (char *)qcvm->edicts)')
run('set $put = ED_FindFunction("PutClientInServer")')
run('call (void)PR_ExecuteProgram($put - qcvm->functions)')
run('set $p->v.weapon = 4096')
run('set $p->v.weaponmodel = PR_SetEngineString("progs/v_axeb.mdl")')
run('set $p->v.think = 549')
run('set $p->v.nextthink = qcvm->time + 1')
run('set $p->v.health = 100')
run('set $p->v.deadflag = 0')
vector('$p->v.view_ofs', (0, 0, 22))
run('set $berserk = GetEdictFieldValueByName($p,"berserk_finished")')
run('set $berserk->_float = qcvm->time + 20')
run('set $customflags = GetEdictFieldValueByName($p,"customflags")')
run('set $customflags->_float = 0')
run('set $cooldown = GetEdictFieldValueByName($p,"attack_finished")')
run('set $cooldown->_float = qcvm->time')
if number('realtime') <= 0:
    run('set realtime = 1')
run('set $client->lastmovetime = realtime')
run('set $client->message.data = $client->msgbuf')
run('set $client->message.maxsize = sizeof($client->msgbuf)')
run('set $client->message.cursize = 0')
run('set $client->cmd.msec = 50')
run('set $client->cmd.seconds = 0.05')
run('set $client->cmd.buttons = 1')
for field in ('vr_active', 'vr_handpos_relative', 'vr_akimbo_active',
              'vr_akimbo_berserk'):
    run('set $client->cmd.%s = 1' % field)
run('set $client->cmd.vr_contact_received = realtime')
run('set $client->cmd.vr_contact.flags = 7')
run('set $client->cmd.vr_contact.weapon = 4096')
run('set $client->cmd.vr_contact.modelindex = SV_ModelIndex("progs/v_axeb.mdl")')

origin = [number('$p->v.origin[%d]' % i) for i in range(3)]
run('set $noop = ED_FindFunction("SUB_Null")')
for hand, y in enumerate((8, -8)):
    name = '$target%d' % hand
    run('set %s = ED_Alloc()' % name)
    run('set %s->v.health = 1000' % name)
    run('set %s->v.takedamage = 1' % name)
    run('set %s->v.solid = 2' % name)
    run('set %s->v.classname = PR_SetEngineString("monster_ogre")' % name)
    for field in ('th_pain', 'th_die'):
        run('set $callback = GetEdictFieldValueByName(%s,"%s")' % (name, field))
        run('set $callback->function = $noop - qcvm->functions')
    vector(name + '->v.origin', (origin[0] + 36, origin[1] + y, origin[2] + 22))
    vector(name + '->v.mins', (-4, -4, -4))
    vector(name + '->v.maxs', (4, 4, 4))
    vector(name + '->v.size', (8, 8, 8))
    run('call (void)SV_LinkEdict(%s, 0)' % name)
    vector('$client->cmd.vr_akimbo_muzzle[%d]' % hand, (8, y, 22))
    vector('$client->cmd.vr_akimbo_angles[%d]' % hand, (0, 0, 0))
    vector('$client->cmd.vr_contact.grip[%d]' % hand, (8, y, 22))
    run('set $client->cmd.vr_contact.speed[%d] = 2' % hand)

def prepare(sequence, x):
    run('set $client->cmd.sequence = %d' % sequence)
    run('set $client->lastmovemessage = %d' % sequence)
    for hand, y in enumerate((8, -8)):
        vector('$client->cmd.vr_contact.base[%d]' % hand, (x, y, 20))
        vector('$client->cmd.vr_contact.tip[%d]' % hand, (x, y, 24))

def sample(sequence, x):
    prepare(sequence, x)
    assert integer('SV_VRContactProcessCommand($client,$p,&$client->cmd)'), \
        'contact invalidated player lifecycle'

sample(1, 24)
assert integer('$client->private_vr_contact_previous_valid')
sample(2, 34)
assert 0 < number('$target0->v.health') < 1000, 'left swept contact did not damage'
assert number('$target1->v.health') == 1000, 'right hand bypassed shared cooldown'
assert integer('$client->private_vr_melee_consumed[0]')
assert integer('$client->private_vr_melee_consumed[1]')
assert integer('SV_VRMeleeSuppressNativeTrigger($client,$p,&$client->cmd)'), \
    'accepted physical contact lost native trigger suppression'

# A pending native callback makes physical damage ineligible, but does not
# revoke valid contact continuity or hand control of the held trigger.
run('set $p->v.think = 438')
sample(3, 34)
assert integer('$client->private_vr_contact_previous_valid'), \
    'pending native callback erased contact continuity'
assert integer('SV_VRMeleeSuppressNativeTrigger($client,$p,&$client->cmd)'), \
    'pending native callback restored a duplicate held trigger'

# A newly completed stroke while native damage is pending is consumed once.
health_before = number('$target0->v.health')
run('call (void)SV_ResetPrivateVRContactState($client)')
sample(4, 24)
sample(5, 34)
assert number('$target0->v.health') == health_before
assert integer('$client->private_vr_melee_consumed[0]')
assert integer('$client->private_vr_melee_consumed[1]')
assert integer('$client->private_vr_contact_previous_valid')

# Reversal can rearm without a sampled stop, after the native attack finishes.
run('set $p->v.think = 549')
run('set $cooldown->_float = qcvm->time')
sample(6, 24)
assert number('$target0->v.health') == health_before
for hand in range(2):
    run('set $target%d->v.origin[0] = %.9g' % (hand, origin[0] + 16))
    run('call (void)SV_LinkEdict($target%d, 0)' % hand)
sample(7, 14)
assert number('$target0->v.health') < health_before, 'reversal did not rearm'
assert number('$target1->v.health') == 1000, 'reversal bypassed shared cooldown'

# The scope must preserve the body before the pinned native strike. The
# maintenance caller supplies the completed command while client movement
# remains zero-duration. Dwell is not currently admitted to the stock WALK
# trial, so exercise its shared pose/Think helper without widening admission.
run('set $ownership = (usercmd_t *)Mem_Alloc(sizeof(usercmd_t))')
run('set *$ownership = $client->cmd')
run('set $client->cmd.msec = 0')
run('set $client->cmd.seconds = 0')
run('set $scope = (sv_vr_weapon_pose_scope_t *)Mem_Alloc(sizeof(sv_vr_weapon_pose_scope_t))')
pose_before = [number('$p->v.origin[%d]' % i) for i in range(3)]
run('call (void)SV_BeginPrivateVRWeaponPose($p,$client,$ownership,$scope)')
assert integer('$scope->dwell_berserk_pose_valid')
assert [number('$p->v.origin[%d]' % i) for i in range(3)] == pose_before
run('call (void)SV_EndPrivateVRWeaponPose($p,$scope)')

class NativeTraceProbe(gdb.Breakpoint):
    def __init__(self):
        super().__init__('PF_traceline', internal=True)
        self.origins = []
    def stop(self):
        if integer('sv_vr_weapon_pose_scope != 0') and \
           integer('sv_vr_weapon_pose_scope->dwell_berserk_pose_valid'):
            self.origins.append([number('$p->v.origin[%d]' % i) for i in range(3)])
        return False

probe = NativeTraceProbe()
for frame, hand_y in ((10, 8), (11, -8)):
    probe.origins.clear()
    run('set $p->v.weaponframe = %d' % frame)
    run('set $p->v.think = 438')
    run('set $p->v.nextthink = qcvm->time')
    assert integer('SV_RunPrivateVRWeaponThink($p,$client,$ownership)')
    assert probe.origins, 'scheduled native strike lost its paired pose'
    expected = [pose_before[0] + 8, pose_before[1] + hand_y, pose_before[2]]
    assert all(abs(a-b) < 0.01 for a, b in zip(probe.origins[0], expected)), \
        'scheduled strike used the wrong anatomical hand'
    assert [number('$p->v.origin[%d]' % i) for i in range(3)] == pose_before
    assert integer('$client->cmd.msec') == 0, 'pose helper changed movement duration'
probe.delete()
run('set $client->cmd = *$ownership')

# Exercise accepted queued contacts through the ordinary physics lifecycle,
# including its PostThink-before-contact ordering and held-trigger refresh.
run('call (void)SV_ResetPrivateVRContactState($client)')
run('set $client->private_vr_contact_spawn_seen = 1')
run('set $p->v.think = 549')
run('set $p->v.nextthink = qcvm->time + 1')
run('set $cooldown->_float = qcvm->time')
run('set $client->cmd.buttons = 0')
run('set $p->v.button0 = 0')
for hand in range(2):
    run('set $target%d->v.health = 1000' % hand)
    run('set $target%d->v.origin[0] = %.9g' % (hand, origin[0] + 36))
    run('call (void)SV_LinkEdict($target%d, 0)' % hand)
prepare(8, 24)
run('set $client->private_cmd_queue[0] = $client->cmd')
prepare(9, 34)
run('set $client->private_cmd_queue[1] = $client->cmd')
run('set $client->private_cmd_queue_head = 0')
run('set $client->private_cmd_queue_count = 2')
run('set $client->private_cmd_queue_msec = 100')
run('set host_frametime = 0')
run('set pr_global_struct->frametime = 0')
run('set host_client = $client')
run('set sv_player = $p')
run('call (void)SV_Physics_Client($p,1)')
assert 0 < number('$target0->v.health') < 1000, 'ordinary physics lost queued hit'
assert number('$target1->v.health') == 1000
ordinary_health = number('$target0->v.health')
run('set $client->cmd.buttons = 1')
run('set $p->v.button0 = 1')
run('set $cooldown->_float = qcvm->time')
run('call (void)SV_Physics_Client($p,1)')
assert number('$target0->v.health') == ordinary_health, 'held trigger duplicated damage'
assert integer('$p->v.think') in (549, 550), 'held trigger scheduled native attack'

# Desktop input still schedules Dwell's native attack with server melee enabled.
run('set $client->private_cmd_queue_count = 0')
run('set $client->private_cmd_queue_msec = 0')
run('call (void)SV_ResetPrivateVRContactState($client)')
run('set $client->cmd.vr_active = 0')
run('set $p->v.button0 = 1')
run('set $cooldown->_float = qcvm->time')
assert not integer('SV_VRMeleeSuppressNativeTrigger($client,$p,&$client->cmd)')
run('call (void)SV_Physics_Client($p,1)')
assert integer('$p->v.think') not in (549, 550), 'desktop native attack was suppressed'
print('VR_DWELL_CONTACT_RUNTIME_PASSED left_health=%g right_health=%g' %
      (number('$target0->v.health'), number('$target1->v.health')))
end
quit 0
