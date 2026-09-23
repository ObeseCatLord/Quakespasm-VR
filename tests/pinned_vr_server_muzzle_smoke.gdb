# Read-only observer for the unchanged 1327f795 dedicated server. Start this
# process first; then run pinned_vr_gameplay_smoke.gdb against its address.
# The JSON is replaced atomically after each matching muzzle reconstruction,
# so the caller can inspect it and stop this server after the client smoke.
#
# QSVR_PINNED_SERVER_RESULT=/caller/path/server.json gdb -nx --batch \
#   -x tests/pinned_vr_server_muzzle_smoke.gdb --args \
#   /tmp/qsvr-reference-1327f795-0s4rAT/Quake/quakespasm-openvr.bin \
#   -dedicated 4 -ip 127.0.0.1 -port 28780 -basedir "$SERVER_PROFILE" \
#   +coop 1 +map e1m1

set pagination off
set confirm off
set debuginfod enabled off
set print thread-events off
set breakpoint pending off

python
import gdb, json, math, os, time

RESULT_PATH = os.environ.get('QSVR_PINNED_SERVER_RESULT')
MIN_VALID_SAMPLES = 3
MAX_STORED_SAMPLES = 64
POSITION_ABS_TOLERANCE = 1.0e-4
POSITION_REL_TOLERANCE = 1.0e-5

result = {
    'status': 'running',
    'scope': 'remote client 0 attack muzzle reconstruction before world clamp',
    'assertion': 'preclamp_muzzle_world ~= restore_origin + server_stored_relative_handpos',
    'minimum_valid_samples': MIN_VALID_SAMPLES,
    'stored_sample_limit': MAX_STORED_SAMPLES,
    'observed_samples': 0,
    'valid_samples': 0,
    'samples_truncated': False,
    'samples': [],
    'claims': ['server stored relative pose and preclamp reconstruction observed'],
    'not_claimed': ['postclamp muzzle origin', 'projectile or shot origin',
                    'damage, ammo, sound, or weapon effects']
}
started = time.monotonic()

def scalar(value):
    return float(value)

def read_vec(value):
    return [scalar(value[i]) for i in range(3)]

def finite3(value):
    return len(value) == 3 and all(math.isfinite(v) for v in value)

def json_vec(value):
    return [v if math.isfinite(v) else None for v in value]

def save_result():
    if not RESULT_PATH:
        return
    temporary = RESULT_PATH + '.tmp.' + str(os.getpid())
    with open(temporary, 'w') as stream:
        json.dump(result, stream, indent=2, sort_keys=True, allow_nan=False)
        stream.write('\n')
    os.replace(temporary, RESULT_PATH)

def fail(message, sample=None):
    result['status'] = 'failed'
    result['error'] = str(message)
    if sample is not None:
        result['failure_sample'] = sample
    try:
        save_result()
    except Exception as exc:
        gdb.write('QSVR_PINNED_SERVER_RESULT_WRITE_FAILED: %s\n' % exc)
    gdb.write('QSVR_PINNED_SERVER_FAILED ' + json.dumps(result, separators=(',', ':')) + '\n')
    return True

def as_int(value):
    return int(value)

class MuzzleObserver(gdb.Breakpoint):
    def stop(self):
        if result['status'] == 'failed':
            return True
        try:
            clients = gdb.parse_and_eval('svs.clients')
            client = clients[0]
            if not (as_int(client['active']) and as_int(client['spawned']) and
                    as_int(client['is_vr_client'])):
                return False
            command = client['cmd']
            if not ((as_int(command['buttons']) & 1) and
                    as_int(command['vr_active']) and
                    as_int(command['vr_handpos_relative']) and
                    as_int(client['vr_handpos_relative'])):
                return False

            entity = gdb.parse_and_eval('ent')
            client_entity = client['edict']
            if not int(entity) or int(entity) != int(client_entity):
                return False

            scope_ptr = gdb.parse_and_eval('sv_vr_weapon_pose_scope')
            if not int(scope_ptr):
                return fail('matching remote attack reached clamp without a pose-restore scope')
            scope = scope_ptr.dereference()
            if not as_int(scope['applied']) or int(scope['ent']) != int(entity):
                return fail('matching remote attack has no applied restore scope for client 0')

            origin = read_vec(scope['origin'])
            relative = read_vec(client['vr_handpos'])
            command_relative = read_vec(command['vr_handpos'])
            muzzle = read_vec(gdb.parse_and_eval('muzzle'))
            stored_aim = read_vec(client['vr_handrot'])
            effective_aim = read_vec(entity.dereference()['v']['v_angle'])
            expected = [origin[i] + relative[i] for i in range(3)]
            error = [muzzle[i] - expected[i] for i in range(3)]
            finite = all(finite3(v) for v in (origin, relative, command_relative,
                                               muzzle, stored_aim, effective_aim,
                                               expected, error))
            close = finite and all(math.isclose(
                muzzle[i], expected[i], rel_tol=POSITION_REL_TOLERANCE,
                abs_tol=POSITION_ABS_TOLERANCE) for i in range(3))
            stored_matches_command = finite and all(math.isclose(
                relative[i], command_relative[i], rel_tol=POSITION_REL_TOLERANCE,
                abs_tol=POSITION_ABS_TOLERANCE) for i in range(3))
            aim_matches_command = finite and all(math.isclose(
                stored_aim[i], effective_aim[i], rel_tol=POSITION_REL_TOLERANCE,
                abs_tol=POSITION_ABS_TOLERANCE) for i in range(2))
            sample = {
                'elapsed_seconds': round(time.monotonic() - started, 6),
                'client_slot': 0,
                'accepted_move_sequence': as_int(client['lastacceptedmovemessage']),
                'attack': True,
                'vr_active': True,
                'command_pose_relative': True,
                'stored_pose_relative': True,
                'restore_origin_world': json_vec(origin),
                'server_stored_handpos_relative': json_vec(relative),
                'command_handpos_relative': json_vec(command_relative),
                'preclamp_muzzle_world': json_vec(muzzle),
                'expected_muzzle_world': json_vec(expected),
                'reconstruction_error': json_vec(error),
                'stored_hand_aim_angles': json_vec(stored_aim),
                'effective_entity_view_angles_at_clamp': json_vec(effective_aim),
                'all_vectors_finite': finite,
                'position_relation_within_tolerance': close,
                'stored_pose_matches_command': stored_matches_command,
                'effective_aim_matches_command_pitch_yaw': aim_matches_command
            }
            result['observed_samples'] += 1
            if len(result['samples']) < MAX_STORED_SAMPLES:
                result['samples'].append(sample)
            else:
                result['samples_truncated'] = True
            if not finite:
                return fail('a matching remote attack sample contains a non-finite vector', sample)
            if not close:
                return fail('preclamp muzzle does not match restore origin plus stored relative hand pose', sample)
            if not stored_matches_command:
                return fail('stored relative hand pose differs from accepted command', sample)
            if not aim_matches_command:
                return fail('weapon-use pitch/yaw differs from accepted hand aim', sample)

            result['valid_samples'] += 1
            if result['valid_samples'] >= MIN_VALID_SAMPLES:
                result['status'] = 'passed'
            try:
                save_result()
            except Exception as exc:
                return fail('could not update result JSON: %s' % exc, sample)
            return False
        except Exception as exc:
            return fail('could not observe donor muzzle reconstruction: %s' % exc)

if not RESULT_PATH:
    result['status'] = 'failed'
    result['error'] = 'QSVR_PINNED_SERVER_RESULT is required'
    gdb.write('QSVR_PINNED_SERVER_FAILED ' + json.dumps(result, separators=(',', ':')) + '\n')
    gdb.execute('quit 1')

try:
    gdb.execute('info address SV_ClampVRMuzzleToWorld', to_string=True)
    observer = MuzzleObserver('SV_ClampVRMuzzleToWorld', internal=True)
except Exception as exc:
    result['status'] = 'failed'
    result['error'] = 'required donor symbol SV_ClampVRMuzzleToWorld is unavailable: %s' % exc
    try:
        save_result()
    except Exception as save_exc:
        gdb.write('QSVR_PINNED_SERVER_RESULT_WRITE_FAILED: %s\n' % save_exc)
    gdb.write('QSVR_PINNED_SERVER_FAILED ' + json.dumps(result, separators=(',', ':')) + '\n')
    gdb.execute('quit 1')

try:
    save_result()
    gdb.execute('run')
except Exception as exc:
    if result['status'] != 'failed':
        fail('dedicated server run stopped with an observer error: %s' % exc)

if result['status'] != 'failed':
    result['observer_finished'] = True
    if result['valid_samples'] < MIN_VALID_SAMPLES:
        result['status'] = 'failed'
        result['error'] = 'server ended before the minimum number of valid muzzle samples'
    try:
        save_result()
    except Exception as exc:
        result['status'] = 'failed'
        result['error'] = 'could not finalize result JSON: %s' % exc

marker = 'QSVR_PINNED_SERVER_PASSED ' if result['status'] == 'passed' else 'QSVR_PINNED_SERVER_FAILED '
gdb.write(marker + json.dumps(result, separators=(',', ':')) + '\n')
end
quit
