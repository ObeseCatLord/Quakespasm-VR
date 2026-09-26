# Headless Enyo katana contact through the command queue drain, real server
# sweeps and native QC. Geometry is injected at the command boundary;
# HMD preparation, packet decoding and visual alignment remain separate.
# Run only where ptrace is available: bash tests/vr_enyo_contact_runtime.sh
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

assert integer('SV_EnyoMeleeProgramLoaded()'), 'installed Enyo revision rejected'
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
run('set pr_global_struct->self = (int)((char *)$p - (char *)qcvm->edicts)')
run('set $put = ED_FindFunction("PutClientInServer")')
run('call (void)PR_ExecuteProgram($put - qcvm->functions)')
run('call (void)Cvar_SetQuick(&sv_immersive_melee,"1")')
run('call (void)Cvar_SetQuick(&sv_nofriendlyfire,"1")')
assert integer('SV_VREnyoMeleeEnabled()'), 'Enyo policy/program not offered'
run('set $p->v.health = 100')
run('set $p->v.deadflag = 0')
run('set $p->v.weapon = 4096')
run('set $p->v.weaponmodel = PR_SetEngineString("progs/ee_v_sword.mdl")')
run('set $p->v.think = 506')  # pinned player_stand1
run('set $p->v.nextthink = qcvm->time + 1')
vector('$p->v.view_ofs', (0, 0, 22))
run('set $cooldown = GetEdictFieldValueByName($p,"attack_finished")')
run('set $switchblock = GetEdictFieldValueByName($p,"switchblock_finished")')
run('set $cooldown->_float = qcvm->time')
run('set $switchblock->_float = 0')
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
run('set $client->cmd.vr_contact.modelindex = SV_ModelIndex("progs/ee_v_sword.mdl")')
assert integer('$client->cmd.vr_contact.modelindex') > 0, 'katana model not precached'
run('set $client->cmd.vr_contact.speed[0] = 5')
vector('$client->cmd.vr_handrot', (0, 0, 0))
vector('$client->cmd.vr_contact.grip[0]', (8, 0, 22))
body = [number('$p->v.origin[%d]' % i) for i in range(3)]

# Three independently reachable slim targets. The first two must be damaged,
# with native hit aftermath admitted between them; the third must remain live.
run('set $noop = ED_FindFunction("SUB_Null")')
for i, (x, y) in enumerate(((33, -12), (38, 12), (42, 0))):
    name = '$target%d' % i
    run('set %s = ED_Alloc()' % name)
    run('set %s->v.health = 1000' % name)
    run('set %s->v.takedamage = 1' % name)
    run('set %s->v.solid = 2' % name)
    run('set %s->v.flags = 32' % name)  # FL_MONSTER, native hit aftermath
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
    run('set $client->private_cmd_queue_head = 0')
    run('set $client->private_cmd_queue_count = 1')
    run('set $client->private_cmd_queue[0] = $client->cmd')
    assert integer('SV_VRContactDrainQueued($p,$client,%d)' % sequence), \
        'queued contact command invalidated player lifecycle'

process(1, 24)
assert integer('$client->private_vr_contact_previous_valid')
assert health() == [1000, 1000, 1000], 'first pose damaged without a sweep'
prepare(2, 44)
assert integer('SV_VRContactTransitionValid($client,$p,&$client->cmd,&$client->private_vr_contact_previous,$client->private_vr_contact_previous_received,$client->private_vr_contact_body_origin)'), \
    'second pose exceeds command continuity bound'

# Preflight real sweep ordering; this avoids crediting the cap for a target
# which was never reachable. Previously hit targets stay solid in the owner.
run('set $trace = (trace_t *)Mem_Alloc(sizeof(trace_t))')
run('set $event = (float *)Mem_Alloc(sizeof(float))')
run('set $blocked = (qboolean *)Mem_Alloc(sizeof(qboolean))')
run('set $excluded = (int *)Mem_Alloc(2*sizeof(int))')
prior = -1.0
for index in range(3):
    found = integer('SV_VRAxeSweep($p,&$client->private_vr_contact_previous,&$client->cmd.vr_contact,0,SV_VR_AXE_SWEEP_DIRECT_EDGE,%g,$excluded,%d,$trace,$event,$blocked)' % \
                    (max(prior, 0.0), index))
    assert found and not integer('*$blocked'), 'unreachable sweep event %d' % index
    assert integer('$trace->ent == $target%d' % index), \
        'sweep order differs at victim %d' % index
    event = number('*$event')
    assert prior <= event <= 1.0
    if index < 2:
        run('set $excluded[%d] = NUM_FOR_EDICT($target%d)' % (index, index))
    prior = event

process(2, 44)
after = health()
assert 0 < after[0] < 1000 and 0 < after[1] < 1000, \
    'native katana failed to damage two distinct victims'
assert after[2] == 1000, 'third victim bypassed stroke cap'
assert integer('$client->private_vr_direct_melee_hit_count[0]') == 2
assert integer('$client->private_vr_direct_melee_subtype[0] == SV_VR_DIRECT_MELEE_ENYO_SWORD')
assert integer('$client->private_vr_melee_consumed[0]')
assert integer('$p->v.think >= 568 && $p->v.think <= 572'), \
    'native sword aftermath was not retained'
assert abs(number('$cooldown->_float - (float)qcvm->time') - .4) < .001
assert number('$switchblock->_float') == number('$cooldown->_float')
assert abs(number('$client->private_vr_direct_melee_deadline[0] - $cooldown->_float')) < .001
assert integer('SV_VRMeleeSuppressNativeTrigger($client,$p,&$client->cmd)'), \
    'qualified physical contact lost native trigger suppression'
process(3, 44)
assert health() == after, 'consumed stroke repeated native damage'

run('set $p->v.weaponmodel = PR_SetEngineString("progs/ee_v_smgs.mdl")')
prepare(4, 44)
assert not integer('SV_VRContactCommandValid($client,$p,&$client->cmd)'), \
    'changed live weapon admitted stale sword contact'
process(4, 44)
assert not integer('$client->private_vr_contact_previous_valid')
assert health() == after
print('ENYO_KATANA_CONTACT_TWO_TARGETS_PASSED')
end
quit
