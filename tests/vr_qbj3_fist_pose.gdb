# Scheduled native QBJ3 fist regression. Reuses the QBJ3 runner and the
# akimbo fixture's private-client/PutClientInServer setup. No XR device needed.
# Run when ptrace is available:
#   tests/vr_qbj3_akimbo_runtime.sh tests/vr_qbj3_fist_pose.gdb
# The installed w_berserk.qc checks 14/34/54/64 before advancing frame;
# W_Fire_Berserker_Multi's traceline2 wrapper reaches PF_traceline.
set pagination off
set confirm off
set debuginfod enabled off
set print thread-events off
set python print-stack full
break SV_Physics
run
python
import gdb

def command(text):
    gdb.execute(text, to_string=True)

def integer(expression):
    return int(gdb.parse_and_eval(expression))

def number(expression):
    return float(gdb.parse_and_eval(expression))

def vector(expression):
    return [number('%s[%d]' % (expression, i)) for i in range(3)]

def set_vector(expression, values):
    for i, value in enumerate(values):
        command('set %s[%d] = %.9g' % (expression, i, value))

def near(actual, expected, tolerance=.03):
    return all(abs(a - b) < tolerance for a, b in zip(actual, expected))

# Mirror vr_qbj3_akimbo_runtime.gdb only through PutClientInServer. The
# program-size/hash assertion prevents this fixture from blessing other QC.
command('set $client = &svs.clients[0]')
command('set $p = EDICT_NUM(1)')
for field in ('active', 'spawned', 'knowntoqc'):
    command('set $client->%s = 1' % field)
command('set $client->edict = $p')
command('set $client->protocol_qsvr = 1')
if number('realtime') <= 0:
    command('set realtime = 1')
command('set $client->lastmovetime = realtime')
command('set $client->message.data = $client->msgbuf')
command('set $client->message.maxsize = sizeof($client->msgbuf)')
command('set $client->datagram.data = $client->datagram_buf')
command('set $client->datagram.maxsize = sizeof($client->datagram_buf)')
assert integer('SV_QBJ3TwinNailgunProgramLoaded()'), 'installed QBJ3 gate rejected'
command('set $animcontroller = GetEdictFieldValueByName($p,"animcontroller")')
command('set $controller = ED_Alloc()')
command('set $controller->v.owner = (int)((char *)$p - (char *)qcvm->edicts)')
command('set $animcontroller->edict = (int)((char *)$controller - (char *)qcvm->edicts)')
command('set pr_global_struct->self = (int)((char *)$p - (char *)qcvm->edicts)')
command('set $put = ED_FindFunction("PutClientInServer")')
command('call (void)PR_ExecuteProgram($put - qcvm->functions)')
vm = gdb.parse_and_eval('qcvm')
assert int(vm['progssize']) == 905470, 'wrong installed QBJ3 progs.dat size'
digest = bytes(int(vm['progssha256'][i]) for i in range(32)).hex()
assert digest == 'de2c6a60df24f5ce0c3fc41b0fd6309105a0ea7ae895dfb4a6867950b9b90e34', \
    'wrong installed QBJ3 progs.dat hash: ' + digest
command('set $loop = ED_FindFunction("weaponanim_berserk_loop")')
assert integer('$loop - qcvm->functions') == 565, 'native berserk loop index changed'

command('set $p->v.health = 100')
command('set $p->v.deadflag = 0')
command('set $p->v.weapon = 4096')
command('set $p->v.weaponmodel = PR_SetEngineString("progs/v_berserk.mdl")')
command('set $p->v.think = $loop - qcvm->functions')
set_vector('$p->v.view_ofs', (0, 0, 22))
set_vector('$p->v.v_angle', (5, 17, 3))
command('set $items = GetEdictFieldValueByName($p,"items_qbj")')
command('set $items->_float = 4')
command('set $finished = GetEdictFieldValueByName($p,"berserk_finished")')
command('set $finished->_float = qcvm->time + 10')
command('set $client->cmd.sequence = 1')
command('set $client->cmd.msec = 50')
command('set $client->cmd.seconds = 0.05')
for field in ('vr_active', 'vr_handpos_relative', 'vr_akimbo_active',
              'vr_akimbo_berserk'):
    command('set $client->cmd.%s = 1' % field)
set_vector('$client->cmd.vr_handpos', (0, 0, 22))
set_vector('$client->cmd.vr_handrot', (0, 0, 30))
for hand, y, yaw, roll in ((0, 8, 90, 90), (1, -8, 270, -90)):
    set_vector('$client->cmd.vr_akimbo_muzzle[%d]' % hand, (0, y, 22))
    set_vector('$client->cmd.vr_akimbo_angles[%d]' % hand, (0, yaw, roll))
command('set $client->cmd.vr_contact_received = realtime')
command('set $ownership = (usercmd_t *)Mem_Alloc(sizeof(usercmd_t))')
command('set *$ownership = $client->cmd')
# Maintenance/weapon Think uses the completed command while movement is zero.
command('set $client->cmd.msec = 0')
command('set $client->cmd.seconds = 0')
body = vector('$p->v.origin')

class NativeFanTrace(gdb.Breakpoint):
    def __init__(self):
        super().__init__('PF_traceline', internal=True)
        self.armed = False
        self.traces = []

    def stop(self):
        if self.armed:
            # OFS_PARM0/1 are QC's native traceline start/end vectors.
            start = [number('qcvm->globals[%d]' % i) for i in range(4, 7)]
            end = [number('qcvm->globals[%d]' % i) for i in range(7, 10)]
            self.traces.append((start, end))
        return False

probe = NativeFanTrace()

def fire(frame, expected_source, expected_direction, expected_right, expect_scope):
    command('set $p->v.weaponframe = %d' % frame)
    command('set $p->v.think = $loop - qcvm->functions')
    command('set $p->v.nextthink = qcvm->time')
    command('set pr_global_struct->self = (int)((char *)$p - (char *)qcvm->edicts)')
    command('set pr_global_struct->time = qcvm->time')
    origin_before = vector('$p->v.origin')
    angles_before = vector('$p->v.v_angle')
    basis_before = {name: vector('pr_global_struct->v_' + name)
                    for name in ('forward', 'right', 'up')}
    now = number('qcvm->time')
    probe.traces.clear()
    probe.armed = True
    try:
        assert integer('SV_RunPrivateVRWeaponThink($p,$client,$ownership)'), \
            'scheduled native Think killed player on frame %d' % frame
    finally:
        probe.armed = False
    assert integer('$p->v.weaponframe') == frame + 1, \
        'QC did not fire before advancing frame %d' % frame
    assert integer('$p->v.think') == 565, 'native loop was not rescheduled'
    assert abs(number('$p->v.nextthink') - now - .05) < .001, \
        'native 0.05-second frame cadence changed'
    assert len(probe.traces) >= 5, 'native five-ray fist fan did not run'
    first_start = probe.traces[0][0]
    assert near(first_start, expected_source), \
        'frame %d traced from %r instead of %r' % (frame, first_start, expected_source)
    # The installed w_berserk.qc source and hash-pinned bytecode (19025-19146)
    # author 70F-30R, 85F-15R, 100F, 85F+15R, 70F+30R. A straight
    # ray alone cannot catch an accidental roll in the native hand pose.
    straight = [(start, end) for start, end in probe.traces
                if near(start, expected_source)]
    forward = [component / 100 for component in expected_direction]
    for forward_scale, right_scale in ((70, -30), (85, -15), (100, 0),
                                       (85, 15), (70, 30)):
        expected_ray = [forward[i] * forward_scale +
                        expected_right[i] * right_scale for i in range(3)]
        assert any(near([end[i] - start[i] for i in range(3)], expected_ray)
                   for start, end in straight), \
            'frame %d lacked authored fan ray %r: %r' % \
            (frame, expected_ray, probe.traces)
    assert near(vector('$p->v.origin'), origin_before, .001), 'body origin leaked'
    assert near(vector('$p->v.v_angle'), angles_before, .001), 'hand angles leaked'
    if expect_scope:
        for name, prior in basis_before.items():
            assert near(vector('pr_global_struct->v_' + name), prior, .001), \
                '%s basis leaked from VR scope' % name
    assert integer('$client->cmd.msec') == 0, 'weapon Think changed movement duration'
    return len(probe.traces)

for frame, hand, direction in ((14, 1, (0, -100, 0)),
                               (64, 1, (0, -100, 0)),
                               (34, 0, (0, 100, 0)),
                               (54, 0, (0, 100, 0))):
    source = [body[0], body[1] + (8 if hand == 0 else -8), body[2] + 22]
    right = (1, 0, 0) if hand == 0 else (-1, 0, 0)
    fire(frame, source, direction, right, True)
    gdb.write('QBJ3_FIST_POSE_PASS frame=%d hand=%d source=%r\n' %
              (frame, hand, source))

# An expired pair must not retain a prior hand. The ordinary dominant-hand
# VR pose is centered at body + (0,0,22), with forward yaw zero.
command('set $ownership->vr_contact_received = realtime - 1')
center = [body[0], body[1], body[2] + 22]
fire(14, center, (100, 0, 0), (0, -1, 0), True)
gdb.write('QBJ3_FIST_STALE_PAIR_FALLBACK_PASS\n')

# Desktop callback still executes the installed native fan without a pair.
command('set $ownership->vr_active = 0')
command('set $client->cmd.vr_active = 0')
set_vector('$p->v.v_angle', (0, 0, 0))
fire(34, center, (100, 0, 0), (0, -1, 0), False)
gdb.write('QBJ3_FIST_DESKTOP_FALLBACK_PASS\n')
probe.delete()
gdb.write('QBJ3_FIST_POSE_RUNTIME_PASS\n')
end
quit 0
