"""Loaded original QC; production codec -> command queue -> contact sweep -> native outcome.
Geometry is injected at the input boundary. No detached damage/physics replica.
Run through Bonk_verify.py; all writes/assets remain in its private build directory.
"""
import gdb
import math


def run(command):
    try:
        return gdb.execute(command, to_string=True)
    except gdb.error:
        print(gdb.execute('bt', to_string=True))
        raise


def integer(expression):
    return int(gdb.parse_and_eval(expression))


def number(expression):
    return float(gdb.parse_and_eval(expression))


def vec(expression, values):
    for i, value in enumerate(values):
        run('set %s[%d] = %.9g' % (expression, i, value))


def vector(expression):
    return [number('%s[%d]' % (expression, i)) for i in range(3)]


assert integer('SV_BonkHammerProgramLoaded()'), 'original installed VM/SHA/ABI rejected'
run('set $c = &svs.clients[0]')
run('set $p = EDICT_NUM(1)')
for field in ('active', 'spawned', 'knowntoqc'):
    run('set $c->%s = 1' % field)
run('set $c->protocol_qsvr = 1')
run('set $c->edict = $p')
run('set $c->message.data = $c->msgbuf')
run('set $c->message.maxsize = sizeof($c->msgbuf)')
run('set $c->datagram.data = $c->datagram_buf')
run('set $c->datagram.maxsize = sizeof($c->datagram_buf)')
run('set pr_global_struct->self = EDICT_TO_PROG($p)')
run('set $put = ED_FindFunction("PutClientInServer")')
run('call (void)PR_ExecuteProgram($put - qcvm->functions)')
run('call (void)Cvar_SetQuick(&sv_immersive_melee,"1")')
run('call (void)Cvar_SetQuick(&sv_nofriendlyfire,"1")')
run('set realtime = 10')
run('set $c->lastmovetime = realtime')
run('set $cool = GetEdictFieldValueByName($p,"attack_finished_hammer")')
run('set $custom = GetEdictFieldValueByName($p,"customflags")')
run('set $source = (usercmd_t *)Mem_Alloc(sizeof(usercmd_t))')
run('set $decoded = (usercmd_t *)Mem_Alloc(sizeof(usercmd_t))')
run('set $buf = (sizebuf_t *)Mem_Alloc(sizeof(sizebuf_t))')
run('set $bytes = (byte *)Mem_Alloc(1400)')
run('set $buf->data = $bytes')
run('set $buf->maxsize = 1400')
run('set $trace = (trace_t *)Mem_Alloc(sizeof(trace_t))')
run('set $noop = ED_FindFunction("SUB_Null")')
body = vector('$p->v.origin')
print('BONK_BODY', body)


def reset(grounded=True):
    run('call (void)SV_ResetPrivateVRContactState($c)')
    run('set $c->private_cmd_queue_head = 0')
    run('set $c->private_cmd_queue_count = 0')
    run('set $c->private_cmd_queue_msec = 0')
    run('set $c->private_discarded_move = 0')
    run('set $c->private_retired_move = 0')
    run('set $c->weapon_contact_last_mode = 6')
    run('set $c->weapon_contact_last_profile = 4')
    run('set $p->v.health = 100')
    run('set $p->v.deadflag = 0')
    run('set $p->v.weapon = 4096')
    run('set $p->v.items = 4096')
    run('set qcvm->globals[873] = 1')
    run('set $p->v.weaponmodel = PR_SetEngineString("progs/v_hammer_default.mdl")')
    run('set $p->v.think = 667')
    run('set $p->v.nextthink = qcvm->time + 1')
    run('set $p->v.flags = %d' % (8 + (512 if grounded else 0)))
    run('set $custom->_float = 0')
    run('set $cool->_float = qcvm->time')
    vec('$p->v.velocity', (0, 0, 0))
    vec('$p->v.v_angle', (11, 17, 3))
    vec('$p->v.origin', body)
    vec('$p->v.view_ofs', (0, 0, 22))
    assert integer('SV_BonkHammerAttackReady($p)')
    run('call (void)memset($source,0,sizeof(usercmd_t))')
    for field, value in (('msec', 125), ('seconds', .125), ('buttons', 1),
                         ('vr_active', 1), ('vr_handpos_relative', 1),
                         ('servertime', 1), ('forwardmove', -77), ('sidemove', 33), ('upmove', -19)):
        run('set $source->%s = %g' % (field, value))
    run('set $source->vr_contact.flags = 13')
    run('set $source->vr_contact.weapon = 4096')
    run('set $source->vr_contact.modelindex = SV_ModelIndex("progs/v_hammer_default.mdl")')
    vec('$source->viewangles', (0, -65, 0))
    vec('$source->vr_handrot', (0, 0, 0))
    vec('$source->vr_contact.head_angles', (0, 90, 0))
    vec('$source->vr_contact.grip[0]', (8, 0, 22))


def process(seq, x, speed, z=22, decode=True):
    run('set $source->vr_contact.speed[0] = %g' % speed)
    vec('$source->vr_contact.base[0]', (x, -16, z))
    vec('$source->vr_contact.tip[0]', (x, 16, z))
    run('set $buf->cursize = 0')
    run('call (void)CL_WritePrivateUsercmd($buf,$source,2,0)')
    run('set net_message = *$buf')
    run('call (void)MSG_BeginReading()')
    valid = integer('SV_ReadPrivateUsercmd($decoded,%d,2,0)' % seq)
    if not decode:
        return valid
    assert valid and integer('msg_readcount == net_message.cursize') and not integer('msg_badread')
    assert vector('$decoded->viewangles') == vector('$source->viewangles')
    for field in ('forwardmove', 'sidemove', 'upmove'):
        assert number('$decoded->' + field) == number('$source->' + field)
    assert integer('SV_QueuePrivateCommand($c,$decoded,realtime)')
    run('set $c->cmd = *$decoded')
    run('set $c->cmd.seconds = .125')
    run('set $c->cmd.vr_contact_received = realtime')
    run('set $c->lastmovemessage = %d' % seq)
    assert integer('SV_VRContactDrainQueued($p,$c,%d)' % seq), 'contact lifecycle failed'
    # Queue retirement is normally owned by the command drain, not this contact-only fixture.
    run('set $c->private_cmd_queue_head = 0')
    run('set $c->private_cmd_queue_count = 0')
    run('set $c->private_cmd_queue_msec = 0')


reset()
assert integer('SV_VRBonkMeleeEnabled()')
process(1, 20, 1)
assert integer('$c->private_vr_contact_previous_valid')
assert integer('SV_VRMeleeSuppressNativeTrigger($c,$p,&$c->cmd)')
run('set $c->cmd.vr_contact.head_angles[1] = 91')
assert not integer('SV_VRMeleeSuppressNativeTrigger($c,$p,&$c->cmd)'), 'duplicate command borrowed accepted head'
run('set $c->cmd.vr_contact.head_angles[1] = 90')
assert integer('SV_VRMeleeSuppressNativeTrigger($c,$p,&$c->cmd)')
print('BONK_ACTUAL_DECODE_HEAD_EQUALITY_PASSED')

# Actual target contact/damage at all three original tiers.
run('set $target = ED_Alloc()')
run('set $target->v.takedamage = 1')
run('set $target->v.solid = 2')
run('set $target->v.flags = 32')
run('set $target->v.classname = PR_SetEngineString("monster_ogre")')
for field in ('th_pain', 'th_die'):
    run('set $callback = GetEdictFieldValueByName($target,"%s")' % field)
    run('set $callback->function = $noop - qcvm->functions')
vec('$target->v.mins', (-2, -2, -2))
vec('$target->v.maxs', (2, 2, 2))
vec('$target->v.size', (4, 4, 4))
vec('$target->v.origin', (body[0] + 33, body[1], body[2] + 22))
run('call (void)SV_LinkEdict($target,0)')
for speed, tier, damage in ((.6, .2, 30), (1, 2, 80), (2.1, 4, 200)):
    reset()
    run('set $target->v.health = 1000')
    process(1, 24, speed)
    process(2, 44, speed)
    assert abs(number('$target->v.health') - (1000 - damage)) < .01, ('tier damage', tier, number('$target->v.health'))
    assert abs(number('$c->private_vr_direct_melee_tier[0]') - tier) < .001
    assert abs(number('$cool->_float - (float)qcvm->time') - .4) < .001
    assert vector('$p->v.v_angle') == [11, 17, 3], 'temporary player angles leaked'
    health = number('$target->v.health')
    process(3, 44, speed)
    assert number('$target->v.health') == health, 'same victim hit twice'
print('BONK_NATIVE_THREE_TIERS_DAMAGE_COOLDOWN_PASSED')

# Terminal whiffs use carried head rather than command or weapon; low dash leaves native Z.
run('set $target->v.solid = 0')
run('call (void)SV_LinkEdict($target,0)')
for grounded in (True, False):
    for speed, tier in ((.6, .2), (1, 2), (2.1, 4)):
        reset(grounded)
        process(1, 20, speed)
        process(2, 24, speed)
        process(3, 24, 0)
        velocity = vector('$p->v.velocity')
        if grounded:
            assert max(abs(v) for v in velocity) < .01, ('ground whiff', velocity)
        else:
            assert abs(velocity[0]) < .01 and abs(velocity[1] - tier * 125) < .01, ('head dash', tier, velocity)
            assert abs(velocity[2] - (0 if tier == .2 else 60)) < .01
        assert abs(number('$cool->_float - (float)qcvm->time') - .4) < .001
        assert not integer('sv_vr_axe_trace_scope.active')
        assert not integer('SV_BonkHammerAttackReady($p)')
print('BONK_NATIVE_GROUND_AIR_WHIFFS_HEAD_FACING_PASSED')
print('BONK_RUNTIME_PASSED')
