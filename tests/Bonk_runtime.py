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
        print('QC_ERROR_SELF_STATEMENT', integer('pr_global_struct->self'), integer('qcvm->xstatement'))
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
run('set pr_global_struct->self = (int)((char *)$p - (char *)qcvm->edicts)')
assert integer('pr_global_struct->self') > 0
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


# Observe temporary VM state across the real outcome call, including its native children.
restore_errors = []
outcome_calls = []
fan_sites = []


def temporary_state():
    fields = ('self', 'other', 'time', 'trace_allsolid', 'trace_startsolid',
              'trace_fraction', 'trace_inwater', 'trace_inopen', 'trace_plane_dist', 'trace_ent')
    values = tuple(number('pr_global_struct->' + f) for f in fields)
    basis = tuple(tuple(vector('pr_global_struct->' + f)) for f in
                  ('v_forward', 'v_right', 'v_up', 'trace_endpos', 'trace_plane_normal'))
    address = integer('&qcvm->globals[1]')
    args = bytes(gdb.selected_inferior().read_memory(address, 27 * 4))
    return values, basis, args, integer('qcvm->argc'), tuple(vector('$p->v.v_angle'))


class OutcomeReturn(gdb.FinishBreakpoint):
    def __init__(self, frame):
        super().__init__(frame, internal=True)
        self.before = temporary_state()

    def stop(self):
        after = temporary_state()
        # QC temporaries are restored while the VM survives. The existing guard
        # must not write a retired/replaced client's edict pose during cleanup.
        if self.before[:4] != after[:4]:
            restore_errors.append('temporary QC globals/arguments leaked')
        owner_live = integer('$c->active && $c->spawned && $c->edict == $p && !$p->free')
        if owner_live and self.before[4] != after[4]:
            restore_errors.append('live owned player angles leaked')
        outcome_calls.append(bool(self.return_value))
        return False


class OutcomeBoundary(gdb.Breakpoint):
    def stop(self):
        OutcomeReturn(gdb.newest_frame())
        return False


class FanBoundary(gdb.Breakpoint):
    def stop(self):
        if integer('sv_vr_axe_trace_scope.active') and integer('sv_vr_axe_trace_scope.mode') == 3:
            fan_sites.append(integer('qcvm->xstatement'))
        return False


outcomes = OutcomeBoundary('SV_VRDirectMeleeOutcome', internal=True)
fans = FanBoundary('SV_BonkHammerWhiffTrace', internal=True)


class RememberCounter(gdb.Breakpoint):
    def __init__(self):
        super().__init__('SV_VRContactRemember', internal=True)
        self.count = 0

    def stop(self):
        self.count += 1
        return False


remember = RememberCounter()


def process(seq, x, speed, z=22, decode=True, received="realtime"):
    run('set $source->vr_contact.speed[0] = %g' % speed)
    vec('$source->vr_contact.base[0]', (x, -16, z))
    vec('$source->vr_contact.tip[0]', (x, 16, z))
    run('set $buf->cursize = 0')
    run('call (void)CL_WritePrivateUsercmd($buf,$source,4,0)')
    run('set net_message = *$buf')
    run('call (void)MSG_BeginReading()')
    valid = integer('SV_ReadPrivateUsercmd($decoded,%d,4,0)' % seq)
    if not decode:
        return valid
    assert valid and integer('msg_readcount == net_message.cursize') and not integer('msg_badread')
    assert vector('$decoded->viewangles') == vector('$source->viewangles')
    for field in ('forwardmove', 'sidemove', 'upmove'):
        assert number('$decoded->' + field) == number('$source->' + field)
    assert integer('SV_QueuePrivateCommand($c,$decoded,%s)' % received)
    run('set $c->cmd = *$decoded')
    run('set $c->cmd.seconds = .125')
    run('set $c->cmd.vr_contact_received = %s' % received)
    run('set $c->lastmovemessage = %d' % seq)
    before = remember.count
    expected_baseline = integer('SV_VRContactCommandValid($c,$p,&$c->cmd)')
    old_sequence = integer('$c->private_vr_contact_last_sequence')
    assert integer('SV_VRContactDrainQueued($p,$c,%d)' % seq), 'contact lifecycle failed'
    if expected_baseline and seq > old_sequence:
        assert remember.count == before + 1, 'normal command tail did not remember exactly once'
        assert integer('$c->private_vr_contact_previous_valid')
        assert integer('$c->private_vr_contact_last_sequence') == seq
        assert integer('SV_VRContactSameSample(&$c->private_vr_contact_previous,&$c->cmd.vr_contact)'), 'next sample lost committing baseline'
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
vec('$target->v.origin', (body[0] + 29, body[1], body[2] + 22))
run('call (void)SV_LinkEdict($target,0)')
for speed, tier, damage in ((.6, .2, 30), (1, 2, 80), (2.1, 4, 200)):
    reset()
    run('set $target->v.health = 1000')
    process(1, 24, speed)
    process(2, 30, speed)
    assert abs(number('$target->v.health') - (1000 - damage)) < .01, ('tier damage', tier, number('$target->v.health'))
    assert abs(number('$c->private_vr_direct_melee_tier[0]') - tier) < .001
    assert abs(number('$cool->_float - (float)qcvm->time') - .4) < .001
    assert vector('$p->v.v_angle') == [11, 17, 3], 'temporary player angles leaked'
    health = number('$target->v.health')
    process(3, 30, speed)
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
        before_fans = len(fan_sites)
        process(3, 24, 0)
        assert fan_sites[before_fans:] == [13531, 13547, 13578, 13616, 13649], 'whiff mask exceeded audited acquisition sites'
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

# Low first outcome locks the tier even when a later distinct victim sees high effort.
reset()
run('set $target->v.solid = 2')
run('set $target->v.health = 1000')
vec('$target->v.origin', (body[0] + 29, body[1] - 10, body[2] + 22))
run('call (void)SV_LinkEdict($target,0)')
run('set $target2 = ED_Alloc()')
run('set $target2->v.takedamage = 1')
run('set $target2->v.solid = 2')
run('set $target2->v.flags = 32')
run('set $target2->v.health = 1000')
run('set $target2->v.classname = PR_SetEngineString("monster_ogre")')
for field in ('th_pain', 'th_die'):
    run('set $callback = GetEdictFieldValueByName($target2,"%s")' % field)
    run('set $callback->function = $noop - qcvm->functions')
vec('$target2->v.mins', (-2, -2, -2))
vec('$target2->v.maxs', (2, 2, 2))
vec('$target2->v.size', (4, 4, 4))
vec('$target2->v.origin', (body[0] + 35, body[1] + 10, body[2] + 22))
run('call (void)SV_LinkEdict($target2,0)')
process(1, 24, .6)
process(2, 30, .6)
assert number('$target->v.health') == 970
process(3, 36, 3)
assert number('$target2->v.health') == 970, 'second victim upgraded first-outcome tier'
assert abs(number('$c->private_vr_direct_melee_tier[0]') - .2) < .001
assert integer('$c->private_vr_direct_melee_hit_count[0]') == 2
for name in ('$target', '$target2'):
    run('set %s->v.solid = 0' % name)
    run('call (void)SV_LinkEdict(%s,0)' % name)
print('BONK_FIRST_OUTCOME_TIER_LOCK_TWO_VICTIMS_PASSED')

# Real world floor sweep calls the native five-argument leaf; its nested floor trace remains native.
run('set $start = (vec3_t *)Mem_Alloc(sizeof(vec3_t))')
run('set $end = (vec3_t *)Mem_Alloc(sizeof(vec3_t))')
vec('(*$start)', (body[0], body[1], body[2] - 24))
vec('(*$end)', (body[0], body[1], body[2] - 88))
run('set *$trace = SV_Move(*$start,vec3_origin,vec3_origin,*$end,1,$p)')
assert 0 <= number('$trace->fraction') < 1 and number('$trace->plane.normal[2]') > .71
floor = number('$trace->endpos[2]') - body[2]
nested_fraction = number('$trace->fraction')
for speed, tier, desired in ((.6, .2, 64), (1, 2, 128), (2.1, 4, 192)):
    reset()
    process(1, 24, speed, floor + 3)
    process(2, 24, speed, floor - 3)
    velocity = vector('$p->v.velocity')
    expected_z = math.sqrt(2 * ((desired + 32) - nested_fraction * 64) * 800)
    assert abs(velocity[0] - tier * -55) < .01 and abs(velocity[1]) < .01
    assert abs(velocity[2] - expected_z) < 1, ('native floor hop', tier, velocity, expected_z)
    assert not integer('(int)$p->v.flags & 512'), 'native airborne flag rolled back'
    assert abs(number('$cool->_float - (float)qcvm->time') - .4) < .001
print('BONK_REAL_SWEEP_NATIVE_FLOOR_HOP_THREE_TIERS_PASSED')

# A same-height wall contact keeps native horizontal recoil but cannot borrow floor launch.
reset()
# Start room walls are found by a production world trace, rather than a fixture collision mock.
wall = None
for axis in (0, 1):
    for sign in (-1, 1):
        start = [body[0], body[1], body[2] + 22]
        end = start[:]
        end[axis] += sign * 1024
        vec('(*$start)', start)
        vec('(*$end)', end)
        run('set *$trace = SV_Move(*$start,vec3_origin,vec3_origin,*$end,1,$p)')
        if 0 < number('$trace->fraction') < 1 and abs(number('$trace->plane.normal[2]')) < .01:
            distance = abs(number('$trace->endpos[%d]' % axis) - body[axis])
            if integer('$trace->ent == qcvm->edicts'):
                wall = (axis, sign, distance)
                break
    if wall:
        break
assert wall, 'fixture player has no reachable world wall'
axis, sign, distance = wall
original_body = body[:]
body[axis] += sign * (distance - 40)
reset()
run('call (void)SV_LinkEdict($p,0)')
if axis == 0:
    process(1, sign * 37, 1)
    process(2, sign * 43, 1)
else:
    # Swap contact axes through the same production encoder/decoder, not another sweep.
    raise AssertionError('expected X wall in original start-room fixture')
assert abs(number('$p->v.velocity[2]')) < .01, 'wall acquired floor launch'
assert abs(number('$p->v.velocity[0]') + 110) < .01
print('BONK_REAL_SWEEP_NATIVE_WALL_NO_FLOOR_LAUNCH_PASSED')
body = original_body
run('call (void)SV_LinkEdict($p,0)')

# Activation rejects sub-.55 jitter even after enough scalar arc accumulates.
reset(False)
for seq, x, speed in ((1, 20, .54), (2, 24, .54), (3, 28, .54), (4, 28, 0)):
    process(seq, x, speed)
assert vector('$p->v.velocity') == [0, 0, 0]
assert number('$cool->_float') == number('(float)qcvm->time')

# Native saf rejects a second dash within .4, then permits it after its own deadline.
reset(False)
process(1, 20, 2.1)
process(2, 24, 2.1)
process(3, 24, 0)
first_velocity = vector('$p->v.velocity')
first_deadline = number('$cool->_float')
process(4, 28, 2.1)
process(5, 32, 2.1)
process(6, 32, 0)
assert vector('$p->v.velocity') == first_velocity and number('$cool->_float') == first_deadline
run('set qcvm->time = qcvm->time + .41')
assert integer('SV_BonkHammerAttackReady($p)')
process(7, 36, 2.1)
process(8, 36, 0)
assert abs(number('$p->v.velocity[1]') - 1000) < .01
print('BONK_JITTER_AND_NATIVE_CADENCE_PASSED')

# Flagged wire framing is independent of current authorization; gameplay is fail closed.
for reason in ('missing_head', 'revoked_cap', 'wrong_model', 'owner_dead'):
    reset(False)
    process(1, 20, 2.1)
    if reason == 'missing_head':
        run('set $source->vr_contact.flags = 5')
    elif reason == 'revoked_cap':
        run('set $c->weapon_contact_last_mode = 2')
    elif reason == 'wrong_model':
        run('set $p->v.weaponmodel = PR_SetEngineString("progs/v_shot.mdl")')
    else:
        run('set $p->v.health = 0')
    process(2, 24, 2.1)
    assert not integer('$c->private_vr_contact_previous_valid'), reason
    assert vector('$p->v.velocity') == [0, 0, 0]
    assert number('$cool->_float') == number('(float)qcvm->time')
run('set $c->lastmovetime = realtime')
reset(False)
run('set *(unsigned int *)&$source->vr_contact.head_angles[0] = 0x7fc00000')
assert not process(1, 20, 1, decode=False) and integer('msg_badread')

# Complete queued-command equality checks the carried head, not only the hand geometry.
reset()
process(1, 20, 1)
run('set $decoded->sequence = 2')
assert integer('SV_QueuePrivateCommand($c,$decoded,realtime)')
run('set $c->cmd = *$decoded')
run('set $c->cmd.vr_contact_received = realtime')
assert integer('SV_VRMeleeSuppressNativeTrigger($c,$p,&$c->cmd)')
run('set $c->cmd.vr_contact.head_angles[1] = 91')
assert not integer('SV_VRMeleeSuppressNativeTrigger($c,$p,&$c->cmd)'), 'queued mutation borrowed another command head'
run('set $c->private_cmd_queue_count = 0')
run('set $c->private_cmd_queue_msec = 0')
print('BONK_MISSING_MALFORMED_REVOKED_OWNER_AND_QUEUED_HEAD_PASSED')

# Production numeric offer/parser/reset; legacy profiles keep their old masks.
reset()
run('set $c->netconnection = (qsocket_t *)Mem_Alloc(sizeof(qsocket_t))')
run('set $c->weapon_contact_last_mode = -1')
run('set $c->weapon_contact_last_profile = -1')
run('set $c->message.cursize = 0')
run('call (void)SV_AppendWeaponContactProtocol($c)')
assert integer('$c->weapon_contact_last_mode & 6') == 6 and integer('$c->weapon_contact_last_profile') == 4
run('set cl.pendingcmd.forwardmove = -77')
run('set cl.pendingcmd.sidemove = 33')
run('set cl.pendingcmd.upmove = -19')
vec('cl.pendingcmd.viewangles', (17, 71, 3))
run('set cl.pendingcmd.vr_contact = $source->vr_contact')
for offer, expected in (('1 6 4', (6, 4)), ('1 2 4', (0, 0)), ('1 6 3', (0, 0)),
                        ('1 2 3', (2, 3)), ('1 2 6', (2, 6)), ('1 0 0', (0, 0)),
                        ('1 6 4 garbage', (0, 0)), ('1 8 4', (0, 0))):
    run('call (void)Cmd_TokenizeString("vr_weapon_contact_protocol %s")' % offer)
    run('set cmd_source = src_server')
    run('call (void)CL_ServerExtension_WeaponContactProtocol_f()')
    assert (integer('cl.vr_weapon_contact_mode'), integer('cl.vr_weapon_contact_profile')) == expected
    assert vector('cl.pendingcmd.viewangles') == [17, 71, 3]
    assert number('cl.pendingcmd.forwardmove') == -77 and number('cl.pendingcmd.sidemove') == 33 and number('cl.pendingcmd.upmove') == -19
assert not integer('cl.pendingcmd.vr_contact.flags')
run('set $old = (unsigned int *)Mem_Alloc(sizeof(unsigned int))')
assert not integer('CL_ParseBoundedDecimal("6",3,$old)'), 'old known-mask client accepted Bonk offer'
print('BONK_PRODUCTION_OFFER_PARSER_RESET_AND_LEGACY_MASKS_PASSED')

# Scope boundary: correct root/sites/player only; all nearby/nested/desktop calls stay native.
reset()
run('set $saved_function = qcvm->xfunction')
run('set $saved_statement = qcvm->xstatement')
run('set $saved_self = pr_global_struct->self')
run('set qcvm->xfunction = &qcvm->functions[470]')
run('set pr_global_struct->self = (int)((char *)$p - (char *)qcvm->edicts)')
run('set sv_vr_axe_trace_scope.active = 1')
run('set sv_vr_axe_trace_scope.mode = 3')
run('set sv_vr_axe_trace_scope.client = $c')
run('set sv_vr_axe_trace_scope.player = $p')
run('set sv_vr_axe_trace_scope.function = qcvm->xfunction')
for site in (13531, 13547, 13578, 13616, 13649):
    run('set qcvm->xstatement = %d' % site)
    assert integer('SV_BonkHammerWhiffTrace($p,0,*$start,*$end,$trace)')
    assert number('$trace->fraction') == 1 and integer('$trace->ent == qcvm->edicts')
for site in (13530, 13532, 13648, 13650):
    run('set qcvm->xstatement = %d' % site)
    run('set $trace->fraction = .375')
    assert not integer('SV_BonkHammerWhiffTrace($p,0,*$start,*$end,$trace)')
    assert number('$trace->fraction') == .375
run('set qcvm->xstatement = 13531')
assert not integer('SV_BonkHammerWhiffTrace(qcvm->edicts,0,*$start,*$end,$trace)')
assert not integer('SV_BonkHammerWhiffTrace($p,1,*$start,*$end,$trace)')
run('set qcvm->xfunction = &qcvm->functions[471]')
assert not integer('SV_BonkHammerWhiffTrace($p,0,*$start,*$end,$trace)'), 'nested floor leaf got forced miss'
run('call (void)SV_VRStockAxeClearTraceScope()')
run('set qcvm->xfunction = &qcvm->functions[470]')
assert not integer('SV_BonkHammerWhiffTrace($p,0,*$start,*$end,$trace)'), 'desktop root got forced miss'
run('set qcvm->xfunction = $saved_function')
run('set qcvm->xstatement = $saved_statement')
run('set pr_global_struct->self = $saved_self')
print('BONK_NATIVE_TRACE_BOUNDARY_AND_TEMPORARY_STATE_PASSED')
assert not restore_errors, restore_errors
assert sum(outcome_calls) >= 17, ('native outcomes were not exercised', outcome_calls)
print('BONK_QUEUE_BASELINE_FIRST_HIT_AND_SETTLED_WHIFF_EXACTLY_ONCE_PASSED')


reset(False)
process(1, 20, 2.1)
process(2, 24, 2.1, received='realtime-.251')
assert not integer('$c->private_vr_contact_previous_valid'), 'expired receipt retained current gameplay authority'
assert vector('$p->v.velocity') == [0, 0, 0]
assert number('$cool->_float') == number('(float)qcvm->time'), 'expired receipt committed cooldown'

# Duplicate command drain cannot overwrite the accepted command's carried head.
reset()
process(1, 20, 1)
run('set $source->vr_contact.head_angles[1] = 91')
before = remember.count
process(1, 20, 1)
assert remember.count == before and number('$c->private_vr_contact_previous.head_angles[1]') == 90
print('BONK_EXPIRED_RECEIPT_AND_DUPLICATE_COMMAND_BASELINE_PASSED')

# Fault injection at a real native callback boundary: invalid owner must still reject the tail.
# Retain native damage already committed before the callback owner disappears.
class InvalidateAfterNative(gdb.FinishBreakpoint):
    def stop(self):
        run('set $c->spawned = 0')
        return False


class InvalidationBoundary(gdb.Breakpoint):
    def stop(self):
        if integer('fnum') == 471:
            InvalidateAfterNative(gdb.newest_frame(), internal=True)
        return False


reset()
run('set $target->v.solid = 2')
run('set $target->v.health = 1000')
vec('$target->v.origin', (body[0] + 29, body[1], body[2] + 22))
run('call (void)SV_LinkEdict($target,0)')
process(1, 24, .6)
# Produce the second command through the codec, but retain it for an invalidated callback.
run('set $source->vr_contact.speed[0] = .6')
vec('$source->vr_contact.base[0]', (30, -16, 22))
vec('$source->vr_contact.tip[0]', (30, 16, 22))
run('set $buf->cursize = 0')
run('call (void)CL_WritePrivateUsercmd($buf,$source,4,0)')
run('set net_message = *$buf')
run('call (void)MSG_BeginReading()')
assert integer('SV_ReadPrivateUsercmd($decoded,2,4,0)')
assert integer('SV_QueuePrivateCommand($c,$decoded,realtime)')
boundary = InvalidationBoundary('PR_ExecuteProgram', internal=True)
before = remember.count
assert not integer('SV_VRContactDrainQueued($p,$c,2)'), 'genuine callback owner loss admitted command tail'
boundary.delete()
assert number('$target->v.health') == 970, 'native damage rolled back after owner loss'
assert remember.count == before and integer('$c->private_vr_contact_last_sequence') == 2
assert number('$c->private_vr_contact_previous.base[0][0]') == 24, 'invalid callback advanced baseline'
assert not restore_errors, restore_errors
assert not integer('sv_vr_axe_trace_scope.active')
print('BONK_REAL_NATIVE_CALLBACK_OWNER_INVALIDATION_REJECTED_PASSED')
print('BONK_RUNTIME_PASSED')
