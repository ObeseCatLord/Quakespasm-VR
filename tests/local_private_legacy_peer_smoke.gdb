# Focused initialized loopback smoke for the private legacy profile.
# QSVR_LOCAL_EXPECT_PRIVATE=1 expects private signon; 0 expects public signon.
# QSVR_LOCAL_MAP_READY optionally names a readiness file for private -> public
# changelevel. QSVR_LOCAL_RESULT is required and receives compact JSON evidence.
set pagination off
set confirm off
set debuginfod enabled off
set breakpoint pending on

python
import gdb, json, math, os, time

expect_text = os.environ.get('QSVR_LOCAL_EXPECT_PRIVATE')
result_path = os.environ.get('QSVR_LOCAL_RESULT')
ready_path = os.environ.get('QSVR_LOCAL_MAP_READY') or None
assert_action_ack = os.environ.get('QSVR_LOCAL_ASSERT_ACTION_ACK') == '1'
assert_move_stats = os.environ.get('QSVR_LOCAL_ASSERT_MOVE_STATS') == '1'
if expect_text not in ('0', '1') or not result_path:
    raise RuntimeError('QSVR_LOCAL_EXPECT_PRIVATE and QSVR_LOCAL_RESULT are required')
expect_private = expect_text == '1'
if ready_path and not expect_private:
    raise RuntimeError('map-switch mode requires a private initial profile')
if ready_path and os.path.exists(ready_path):
    raise RuntimeError('remove the old map readiness file before starting')
if assert_action_ack and not expect_private:
    raise RuntimeError('action/ACK probe requires a private peer')
if assert_move_stats and not expect_private:
    raise RuntimeError('movement-stat probe requires a private peer')

started = time.monotonic()
phase = 'signon'
phase_time = started
samples = []
failure = None
first_attack_seq = None
first_attack_shells = None
first_shell_ack = None

def integer(expr):
    return int(gdb.parse_and_eval(expr))

def world_name():
    return os.path.basename(gdb.parse_and_eval('cl.worldmodel->name').string())

def sample(label):
    owner = integer('cl.viewentity')
    origin = [float(gdb.parse_and_eval('cl.entities[%d].netstate.origin[%d]' %
                                       (owner, axis))) for axis in range(3)]
    return dict(label=label, signon=integer('cls.signon'),
                dialect=integer('cl.protocol_qsvr'),
                legacy=integer('cls.legacy_qsvr'),
                permission=bool(integer('cl.move_ack_prediction_allowed')),
                origin=origin, shells=integer('cl.stats[6]'),
                ack=integer('cl.ackedmovemessages'),
                sent=integer('cl.movemessages'), world=world_name())

class CheckFailure(Exception):
    pass

def require(ok, name):
    if not ok:
        raise CheckFailure(name)

def check_authority(state, private):
    require(state['signon'] == 4, 'signon')
    require(state['dialect'] == int(private) and state['legacy'] == 0,
            'protocol_authority')
    require(not state['permission'], 'prediction_permission')

def check_settled_pair(before, settled, private):
    check_authority(before, private)
    check_authority(settled, private)
    distance = math.sqrt(sum((a - b) ** 2 for a, b in
                             zip(before['origin'], settled['origin'])))
    require(distance > 50.0, 'settled_movement')
    require(settled['shells'] < before['shells'], 'shell_consumption')
    require(settled['ack'] > before['ack'] and settled['ack'] <= settled['sent'],
            'move_ack')
    return round(distance, 3)

def movement_stats():
    return dict(flags=integer('cl.stats[225]'),
                gravity=float(gdb.parse_and_eval('cl.statsf[242]')),
                maxspeed=float(gdb.parse_and_eval('cl.statsf[244]')),
                jumpspeed=float(gdb.parse_and_eval('cl.statsf[250]')),
                stepheight=float(gdb.parse_and_eval('cl.statsf[253]')))

class HostFrame(gdb.Breakpoint):
    def stop(self):
        global phase, phase_time, failure, first_attack_seq, first_attack_shells, first_shell_ack
        now = time.monotonic()
        if now - started > 120:
            failure = 'timeout'
            return True
        try:
            if first_attack_seq is not None and first_shell_ack is None and \
                    integer('cl.stats[6]') < first_attack_shells:
                first_shell_ack = integer('cl.ackedmovemessages')
                require(first_shell_ack >= first_attack_seq,
                        'attack_effect_before_completed_ack')
            if phase == 'signon':
                if integer('cls.signon') == 4:
                    phase, phase_time = 'baseline_wait', now
            elif phase == 'baseline_wait' and now - phase_time >= 1:
                samples.append(sample('before'))
                if assert_action_ack:
                    first_attack_seq = integer('cl.movemessages')
                    first_attack_shells = samples[-1]['shells']
                gdb.execute('set in_forward.state = 1', to_string=True)
                gdb.execute('set in_attack.state = 1', to_string=True)
                phase, phase_time = 'movement', now
            elif phase == 'movement' and now - phase_time >= 2:
                gdb.execute('set in_forward.state = 0', to_string=True)
                gdb.execute('set in_attack.state = 0', to_string=True)
                phase, phase_time = 'settling', now
            elif phase == 'settling' and now - phase_time >= 1:
                samples.append(sample('settled'))
                if ready_path:
                    try:
                        check_settled_pair(samples[0], samples[1], True)
                    except CheckFailure as exc:
                        failure = str(exc)
                        return True
                    with open(ready_path, 'w') as marker:
                        marker.write('ready\n')
                    phase, phase_time = 'world_change', now
                else:
                    return True
            elif phase == 'world_change' and integer('cls.signon') == 4:
                if world_name() != samples[0]['world']:
                    phase, phase_time = 'after_world_wait', now
            elif phase == 'after_world_wait' and now - phase_time >= 1:
                samples.append(sample('after_changelevel'))
                return True
        except CheckFailure as exc:
            failure = str(exc)
            return True
        except Exception:
            failure = 'probe_state_error'
            return True
        return False

HostFrame('Host_Frame', internal=True)
gdb.Breakpoint('Host_Error', internal=True)
gdb.Breakpoint('Sys_Error', internal=True)
end
run
python
try:
    frame = gdb.newest_frame()
    completed_at_host_frame = frame is not None and frame.name() == 'Host_Frame'
    if failure is None and not completed_at_host_frame:
        failure = 'inferior_stopped_before_completion'
    if failure is None:
        require(len(samples) == (3 if ready_path else 2), 'sample_count')
        movement = check_settled_pair(samples[0], samples[1], expect_private)
        if assert_move_stats:
            exported_move = movement_stats()
            require(exported_move['flags'] & 0x80000000, 'missing_moveflags_valid')
            for key, expected in [('gravity', 800.0), ('maxspeed', 320.0),
                                  ('jumpspeed', 270.0), ('stepheight', 18.0)]:
                require(math.isfinite(exported_move[key]) and
                        abs(exported_move[key] - expected) < 0.01,
                        'missing_or_wrong_' + key)
        if assert_action_ack:
            require(first_shell_ack is not None, 'no_attack_effect_ack_pair')
        if ready_path:
            after = samples[2]
            check_authority(after, False)
            require(after['world'] != samples[0]['world'], 'world_change')
            require(after['ack'] <= after['sent'], 'post_change_ack')
except CheckFailure as exc:
    failure = str(exc)
except Exception:
    failure = 'probe_validation_error'

outcome = 'failed' if failure else 'passed'
result = dict(status=outcome,
              mode='map_switch' if ready_path else ('private' if expect_private else 'public'),
              samples=[{key: value for key, value in item.items() if key != 'origin'}
                       for item in samples])
if failure:
    result['failure'] = failure
else:
    result['settled_displacement'] = movement
    if assert_action_ack:
        result['first_attack_sequence'] = first_attack_seq
        result['first_shell_effect_ack'] = first_shell_ack
    if assert_move_stats:
        result['movement_stats'] = exported_move
temporary = result_path + '.tmp.' + str(os.getpid())
with open(temporary, 'w') as output:
    json.dump(result, output, indent=2, sort_keys=True)
    output.write('\n')
os.replace(temporary, result_path)
if failure:
    gdb.write('QSVR_LOCAL_LEGACY_FAILED ' + failure + '\n', gdb.STDERR)
    gdb.execute('quit 1')
marker = 'QSVR_LOCAL_MAP_SWITCH_PASSED' if ready_path else (
    'QSVR_LOCAL_PRIVATE_PASSED' if expect_private else 'QSVR_LOCAL_PUBLIC_PASSED')
gdb.write(marker + '\n')
end
quit 0
