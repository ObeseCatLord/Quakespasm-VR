# Headless installed-QBJ3 wrench contact. This drives the accepted command
# processor and real server traces, then checks native QC leaf damage. No HMD.
# Run only when ptrace is available: tests/vr_qbj3_contact_runtime.sh
set pagination off
set confirm off
set debuginfod enabled off
set print thread-events off
set python print-stack full
break SV_Physics
run
python
import gdb

def run(text):
    gdb.execute(text, to_string=True)

def integer(expression):
    return int(gdb.parse_and_eval(expression))

def number(expression):
    return float(gdb.parse_and_eval(expression))

def vector(expression, values):
    for axis, value in enumerate(values):
        run('set %s[%d] = %.9g' % (expression, axis, value))

def health():
    return [number('$target%d->v.health' % i) for i in range(3)]

run('set $client = &svs.clients[0]')
run('set $p = EDICT_NUM(1)')
for field in ('active', 'spawned', 'knowntoqc'):
    run('set $client->%s = 1' % field)
run('set $client->protocol_qsvr = 1')
run('set $client->edict = $p')
run('set $client->message.data = $client->msgbuf')
run('set $client->message.maxsize = sizeof($client->msgbuf)')
run('set $client->datagram.data = $client->datagram_buf')
run('set $client->datagram.maxsize = sizeof($client->datagram_buf)')
assert integer('SV_QBJ3TwinNailgunProgramLoaded()'), 'installed QBJ3 gate rejected'
vm = gdb.parse_and_eval('qcvm')
assert int(vm['progssize']) == 905470, 'unexpected QBJ3 program size'
digest = bytes(int(vm['progssha256'][i]) for i in range(32)).hex()
assert digest == 'de2c6a60df24f5ce0c3fc41b0fd6309105a0ea7ae895dfb4a6867950b9b90e34', \
    'unexpected QBJ3 program hash: ' + digest

# The installed QC uses this controller during PutClientInServer.
run('set $animcontroller = GetEdictFieldValueByName($p,"animcontroller")')
run('set $controller = ED_Alloc()')
run('set $controller->v.owner = (int)((char *)$p - (char *)qcvm->edicts)')
run('set $animcontroller->edict = (int)((char *)$controller - (char *)qcvm->edicts)')
run('set pr_global_struct->self = (int)((char *)$p - (char *)qcvm->edicts)')
run('set $put = ED_FindFunction("PutClientInServer")')
run('call (void)PR_ExecuteProgram($put - qcvm->functions)')
run('call (void)Cvar_SetQuick(&sv_immersive_melee,"1")')
run('call (void)Cvar_SetQuick(&sv_nofriendlyfire,"1")')
assert integer('SV_VRQBJ3MeleeEnabled()'), 'pinned program/policy did not enable contact'
run('set $p->v.health = 100')
run('set $p->v.deadflag = 0')
run('set $p->v.weapon = 4096')
run('set $p->v.weaponmodel = PR_SetEngineString("progs/v_wrench.mdl")')
run('set $p->v.think = 569')
run('set $p->v.nextthink = qcvm->time + 1')
vector('$p->v.view_ofs', (0, 0, 22))
run('set $items = GetEdictFieldValueByName($p,"items_qbj")')
run('set $items->_float = 0')
run('set $finished = GetEdictFieldValueByName($p,"berserk_finished")')
run('set $finished->_float = 0')
run('set $cooldown = GetEdictFieldValueByName($p,"attack_finished")')
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
run('set $client->cmd.vr_contact.flags = 5')  # left valid | immersive melee
run('set $client->cmd.vr_contact.weapon = 4096')
run('set $client->cmd.vr_contact.modelindex = SV_ModelIndex("progs/v_wrench.mdl")')
assert integer('$client->cmd.vr_contact.modelindex') > 0, 'wrench model not precached'
run('set $client->cmd.vr_contact.speed[0] = 5')
vector('$client->cmd.vr_handrot', (0, 0, 0))
vector('$client->cmd.vr_contact.grip[0]', (8, 0, 22))
body = [number('$p->v.origin[%d]' % i) for i in range(3)]

# Three slim, stationary damageable entities each intersect a different
# point on the moving edge. Their separate Y offsets keep reach traces clear.
# The third is reachable after the second, so the cap has real work to do.
run('set $noop = ED_FindFunction("SUB_Null")')
for i, (x, y) in enumerate(((33, -12), (38, 12), (42, 0))):
    name = '$target%d' % i
    run('set %s = ED_Alloc()' % name)
    run('set %s->v.health = 1000' % name)
    run('set %s->v.takedamage = 1' % name)
    run('set %s->v.solid = 2' % name)
    run('set %s->v.classname = PR_SetEngineString("monster_ogre")' % name)
    for field in ('th_pain', 'th_die'):
        run('set $callback = GetEdictFieldValueByName(%s,"%s")' % (name, field))
        run('set $callback->function = $noop - qcvm->functions')
    vector(name + '->v.origin', (body[0] + x, body[1] + y, body[2] + 22))
    vector(name + '->v.mins', (-2, -2, -2))
    vector(name + '->v.maxs', (2, 2, 2))
    vector(name + '->v.size', (4, 4, 4))
    run('call (void)SV_LinkEdict(%s,0)' % name)

def prepare(sequence, x):
    run('set $client->cmd.sequence = %d' % sequence)
    run('set $client->lastmovemessage = %d' % sequence)
    vector('$client->cmd.vr_contact.base[0]', (x, -16, 22))
    vector('$client->cmd.vr_contact.tip[0]', (x, 16, 22))

def process(sequence, x):
    prepare(sequence, x)
    assert integer('SV_VRContactProcessCommand($client,$p,&$client->cmd)'), \
        'contact command invalidated the player lifecycle'

process(1, 24)
assert integer('$client->private_vr_contact_previous_valid'), \
    'first valid pose did not establish continuity'
assert health() == [1000, 1000, 1000], 'first pose damaged without a sweep'
prepare(2, 44)
assert integer('SV_VRContactTransitionValid($client,$p,&$client->cmd,&$client->private_vr_contact_previous,$client->private_vr_contact_previous_received,$client->private_vr_contact_body_origin)'), \
    'second pose exceeds the accepted command continuity bound'

# Preflight the actual server sweep with successive exclusions. This proves
# that each proposed victim is a reachable later event before damage is run.
run('set $trace = (trace_t *)Mem_Alloc(sizeof(trace_t))')
run('set $event = (float *)Mem_Alloc(sizeof(float))')
run('set $blocked = (qboolean *)Mem_Alloc(sizeof(qboolean))')
run('set $excluded = (int *)Mem_Alloc(2*sizeof(int))')
prior = -1.0
for index in range(3):
    found = integer('SV_VRAxeSweep($p,&$client->private_vr_contact_previous,&$client->cmd.vr_contact,0,2,%g,$excluded,%d,$trace,$event,$blocked)' % \
                    (max(prior, 0.0), index))
    assert found and not integer('*$blocked'), \
        'test geometry has no reachable sweep event %d' % index
    assert integer('$trace->ent == $target%d' % index), \
        'test geometry sweep order differs at victim %d' % index
    event = number('*$event')
    assert event >= prior and event <= 1.0, 'sweep event order changed'
    if index < 2:
        run('set $excluded[%d] = NUM_FOR_EDICT($target%d)' % (index, index))
    prior = event

assert integer('SV_VRContactProcessCommand($client,$p,&$client->cmd)'), \
    'accepted QBJ3 callback invalidated player lifecycle'
assert health() == [940, 940, 1000], \
    'native wrench leaves did not damage exactly two distinct victims'
assert integer('$client->private_vr_qbj3_hit_count[0]') == 2
assert integer('$client->private_vr_melee_consumed[0]'), 'third victim bypassed cap'
assert integer('$client->private_vr_qbj3_hit_entities[0][0] == NUM_FOR_EDICT($target0)')
assert integer('$client->private_vr_qbj3_hit_entities[0][1] == NUM_FOR_EDICT($target1)')
assert abs(number('$cooldown->_float') - number('qcvm->time') - .8) < .001, \
    'follow-up restarted native wrench recovery'
assert integer('SV_VRMeleeSuppressNativeTrigger($client,$p,&$client->cmd)'), \
    'accepted physical contact lost duplicate native trigger suppression'
process(3, 44)
assert health() == [940, 940, 1000], 'consumed stroke dealt duplicate damage'

# A selected berserk model cannot authorize a stale wrench contact profile.
run('set $p->v.weaponmodel = PR_SetEngineString("progs/v_berserk.mdl")')
run('set $items->_float = 4')
prepare(4, 44)
assert not integer('SV_VRContactCommandValid($client,$p,&$client->cmd)'), \
    'wrong selected model was accepted as wrench contact'
assert integer('SV_VRContactProcessCommand($client,$p,&$client->cmd)')
assert not integer('$client->private_vr_contact_previous_valid')
assert health() == [940, 940, 1000]

run('set $p->v.weaponmodel = PR_SetEngineString("progs/v_wrench.mdl")')
run('set $items->_float = 0')
run('set $client->cmd.vr_contact_received = realtime - 1')
prepare(5, 44)
assert not integer('SV_VRContactCommandValid($client,$p,&$client->cmd)'), \
    'stale received time was accepted'
assert integer('SV_VRContactProcessCommand($client,$p,&$client->cmd)')
assert not integer('$client->private_vr_contact_previous_valid')
assert health() == [940, 940, 1000]

# A rejected physical command must leave the ordinary held-trigger route
# available. The installed QC schedules weaponanim_wrench_loop from its
# W_WeaponFrame -> W_Attack -> W_AxeSwing dispatch in PlayerPostThink.
run('set $native_loop = ED_FindFunction("weaponanim_wrench_loop")')
run('set $p->v.think = 569')
run('set $p->v.nextthink = qcvm->time + 1')
run('set $p->v.weaponframe = 10')
run('set $cooldown->_float = qcvm->time')
run('set $p->v.button0 = 1')
run('set host_frametime = 0')
run('set pr_global_struct->frametime = 0')
run('set host_client = $client')
run('set sv_player = $p')
assert not integer('SV_VRMeleeSuppressNativeTrigger($client,$p,&$client->cmd)'), \
    'stale physical command suppressed the ordinary held trigger'
run('call (void)SV_Physics_Client($p,1)')
assert integer('$p->v.think == $native_loop - qcvm->functions'), \
    'stale physical command did not schedule the native wrench attack'
assert health() == [940, 940, 1000], 'native scheduling damaged before strike frame'

run('set $client->cmd.vr_contact_received = realtime')
run('call (void)Cvar_SetQuick(&sv_immersive_melee,"0")')
prepare(6, 44)
assert not integer('SV_VRQBJ3MeleeEnabled()'), 'server melee policy stayed active'
assert not integer('SV_VRContactCommandValid($client,$p,&$client->cmd)'), \
    'unoffered QBJ3 melee profile accepted physical contact'
assert not integer('SV_VRMeleeSuppressNativeTrigger($client,$p,&$client->cmd)'), \
    'unoffered QBJ3 melee profile suppressed the native trigger'
assert integer('SV_VRContactProcessCommand($client,$p,&$client->cmd)')
assert health() == [940, 940, 1000]
run('call (void)Cvar_SetQuick(&sv_immersive_melee,"1")')

run('set $client->cmd.vr_contact_received = realtime')
run('set $client->cmd.vr_active = 0')
prepare(7, 44)
assert not integer('SV_VRContactCommandValid($client,$p,&$client->cmd)'), \
    'desktop command entered VR contact'
assert not integer('SV_VRMeleeSuppressNativeTrigger($client,$p,&$client->cmd)'), \
    'desktop held trigger was suppressed by VR contact'
assert integer('SV_VRContactProcessCommand($client,$p,&$client->cmd)')
run('set $p->v.think = 569')
run('set $p->v.nextthink = qcvm->time + 1')
run('set $p->v.weaponframe = 10')
run('set $cooldown->_float = qcvm->time')
run('set $p->v.button0 = 1')
run('call (void)SV_Physics_Client($p,1)')
assert integer('$p->v.think == $native_loop - qcvm->functions'), \
    'desktop held trigger did not schedule the native wrench attack'
assert health() == [940, 940, 1000]

# A native hit can make side effects before the adapter returns false. Inject
# owner changes at that boundary and require both follow-up damage and the
# other physical hand to stop. These are explicit fault-injection scenarios,
# not claims that the installed mod naturally executes those mutations.
class ContactHands(gdb.Breakpoint):
    def __init__(self):
        super().__init__('SV_VRContactProcessQBJ3Melee', internal=True)
        self.hands = []

    def stop(self):
        self.hands.append(integer('hand'))
        return False

class ChangeOwnerAtLeafReturn(gdb.Breakpoint):
    def __init__(self, leaf, action):
        super().__init__('SV_VRAxeTraceLeaveFunction', internal=True)
        self.leaf, self.action, self.injected = leaf, action, False

    def stop(self):
        if not self.injected and integer(
                'qcvm->xfunction == &qcvm->functions[%d]' % self.leaf):
            self.injected = True
            run(self.action)
        return False

faults = (
    (False, 'relocated wrench', 'set $p->v.origin[0] += 1'),
    (True, 'relocated fists', 'set $p->v.origin[0] += 1'),
    (True, 'reset fists', 'call (void)SV_ResetPrivateVRContactState($client)'),
    (True, 'dead fists', 'set $p->v.health = 0'),
    (True, 'changed fists', 'set $items->_float = 0'),
)
for case, (paired, label, action) in enumerate(faults):
    run('call (void)SV_ResetPrivateVRContactState($client)')
    vector('$p->v.origin', body)
    run('call (void)SV_LinkEdict($p,0)')
    run('set $p->v.health = 100')
    run('set $p->v.deadflag = 0')
    run('set $p->v.think = 569')
    run('set $p->v.nextthink = qcvm->time + 1')
    run('set $p->v.weaponframe = 10')
    run('set $p->v.weaponmodel = PR_SetEngineString("progs/v_%s.mdl")' %
        ('berserk' if paired else 'wrench'))
    run('set $items->_float = %d' % (4 if paired else 0))
    run('set $finished->_float = 0')
    run('set $cooldown->_float = qcvm->time')
    run('set $client->lastmovetime = realtime')
    run('set $client->cmd.vr_contact_received = realtime')
    run('set $client->cmd.vr_active = 1')
    run('set $client->cmd.vr_akimbo_active = %d' % paired)
    run('set $client->cmd.vr_akimbo_berserk = %d' % paired)
    run('set $client->cmd.vr_contact.flags = %d' % (7 if paired else 5))
    run('set $client->cmd.vr_contact.modelindex = SV_ModelIndex("progs/v_%s.mdl")' %
        ('berserk' if paired else 'wrench'))
    for target in range(3):
        run('set $target%d->v.health = 1000' % target)
    for hand in range(2 if paired else 1):
        y = (-12 if hand == 0 else 12) if paired else 0
        vector('$client->cmd.vr_contact.grip[%d]' % hand, (8, y, 22))
        vector('$client->cmd.vr_akimbo_muzzle[%d]' % hand, (8, y, 22))
        vector('$client->cmd.vr_akimbo_angles[%d]' % hand, (0, 0, 0))
        run('set $client->cmd.vr_contact.speed[%d] = 5' % hand)
    for step, x in enumerate((24, 44)):
        sequence = 10 + case * 2 + step
        prepare(sequence, x)
        if paired:
            for hand, y in enumerate((-12, 12)):
                vector('$client->cmd.vr_contact.base[%d]' % hand, (x, y-2, 22))
                vector('$client->cmd.vr_contact.tip[%d]' % hand, (x, y+2, 22))
        if step == 0:
            assert integer('SV_VRContactProcessCommand($client,$p,&$client->cmd)')
            assert integer('$client->private_vr_contact_previous_valid'), label
            continue
        hands = ContactHands()
        fault = ChangeOwnerAtLeafReturn(572 if paired else 485, action)
        try:
            run('set $processed = SV_VRContactProcessCommand($client,$p,&$client->cmd)')
            assert fault.injected, label + ': native leaf was not exercised'
            assert hands.hands == [0], label + ': second hand ran after invalidation'
            assert health() == [520 if paired else 940, 1000, 1000], \
                label + ': damage continued after invalidation'
            assert not integer('$client->private_vr_contact_previous_valid'), label
            assert not integer('$client->private_vr_qbj3_authorized[0]'), label
            assert integer('$client->private_vr_qbj3_hit_count[0]') == 0, label
        finally:
            fault.delete()
            hands.delete()
print('QBJ3_CONTACT_RUNTIME_PASSED')
end
quit 0
