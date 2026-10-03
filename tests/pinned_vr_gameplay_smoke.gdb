# Focused live OpenXR/private-peer smoke. Caller supplies runtime environment,
# basedir, and client arguments (including +connect <address:port> qsvr1).
# Synthetic controller poses/actions are injected. Optional controlled ramps
# adjust completed-frame HMD x/z; the physical runtime remains live. Input/key
# binding, command builder, transport, pinned server, and PMove run normally.
# No usercmd or server state is injected. Geometry and roomscale qualification
# are follow-up work.
#
# QSVR_PINNED_VR_RESULT=/caller/path/result.json timeout 120s gdb -nx --batch \
#   -x tests/pinned_vr_gameplay_smoke.gdb --args "$BINARY" -openxr \
#   -nosound -window -width 640 -height 480 -basedir "$PROFILE" <client args>

set pagination off
set confirm off
set debuginfod enabled off
set print thread-events off

python
import gdb, json, math, os, struct, time

started = time.monotonic()
phase = 'neutral'
actions = 0
neutral_actions = 0
armed = False
wire = {}
lifecycle_hook = None
head_z_anchor = None
result = {'status':'running', 'scope':'focused XR admission, private VR command, shotgun shell consumption',
          'follow_up':['body/eye/muzzle geometry', 'roomscale movement and collision',
                       'weapon damage/effects and physical headset/controller qualification']}
result_path = os.environ.get('QSVR_PINNED_VR_RESULT')

# Preserve contemporaneous native failure ownership before batch cleanup.
def capture_abort_stack(event):
    if isinstance(event, gdb.SignalEvent) and event.stop_signal == 'SIGABRT':
        text = gdb.execute('thread apply all bt', to_string=True)
        if result_path:
            with open(result_path + '.abort-stack.txt', 'w') as output:
                output.write(text)
gdb.events.stop.connect(capture_abort_stack)

expect_prediction_text = os.environ.get('QSVR_PINNED_VR_EXPECT_SELECTED_PREDICTION', '0')
if expect_prediction_text not in ('0', '1'):
    raise RuntimeError('QSVR_PINNED_VR_EXPECT_SELECTED_PREDICTION must be 0 or 1')
expect_selected_prediction = expect_prediction_text == '1'
ramp_text = os.environ.get('QSVR_PINNED_VR_HEAD_RAMP_METERS_PER_ACTION', '0')
try:
    head_ramp_rate = float(ramp_text)
except ValueError:
    head_ramp_rate = None
head_z_ramp_text = os.environ.get('QSVR_PINNED_VR_HEAD_Z_METERS_PER_ACTION')
try:
    head_z_ramp_rate = float(head_z_ramp_text) if head_z_ramp_text is not None else None
except ValueError:
    head_z_ramp_rate = None
prediction_previous = None
prediction_between_send = None
prediction_probe = dict(replay_returns=0, replay_success=0, return_capture_errors=0,
                        call_probe_errors=0, rendered_frames=0,
                        frames_with_matching_return=0, matching_successful_returns=0,
                        matching_owner_returns=0, unchanged_ack_sent_authority_pairs=0,
                        replay_proven_stable_pairs=0,
                        max_stable_candidate_displacement=0.0,
                        max_replay_proven_stable_displacement=0.0)
last_replay_result = None
last_render_framecount = None
fire_prediction_command = None

def float32(value): return struct.unpack('f', struct.pack('f', value))[0]

def iv(expr): return int(gdb.parse_and_eval(expr))
def fv(expr): return float(gdb.parse_and_eval(expr))
def vec(expr): return [fv('%s[%d]' % (expr, i)) for i in range(3)]
def finite3(v): return len(v) == 3 and all(math.isfinite(x) for x in v)
def distance3(a, b): return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))
def cbuf(text):
    gdb.execute('call (void)Cbuf_AddText(' + json.dumps(text) + ')', to_string=True)
    gdb.execute('call (void)Cbuf_Execute()', to_string=True)

def inject():
    global actions, neutral_actions, armed, head_z_anchor
    try:
        if int(gdb.parse_and_eval('(unsigned long)frame')) != int(gdb.parse_and_eval('(unsigned long)&openxr_frame')):
            raise RuntimeError('input did not receive the live OpenXR frame')
        if not (iv('vulkan_globals.stereo_active') and iv('openxr_frame.should_render') and
                iv('openxr_frame.focused') and iv('openxr_frame.devices[0].valid')):
            raise RuntimeError('focused stereo/HMD admission was lost')
        hmd = [[fv('openxr_frame.devices[0].matrix[%d][%d]' % (r,c)) for c in range(4)] for r in range(3)]
        action_sample = actions + 1
        ramp_steps = max(0, action_sample - 25)
        ramp_offset = ramp_steps * head_ramp_rate
        expected_hmd = [row[:] for row in hmd]
        if ramp_offset:
            expected_hmd[0][3] = float32(hmd[0][3] + ramp_offset)
            gdb.execute('set openxr_frame.devices[0].matrix[0][3] = %.9g' % expected_hmd[0][3],
                        to_string=True)
        z_ramp_offset = ramp_steps * head_z_ramp_rate if head_z_ramp_rate is not None else 0.0
        if head_z_ramp_rate is not None:
            if head_z_anchor is None:
                head_z_anchor = hmd[2][3]
            expected_hmd[2][3] = float32(head_z_anchor + z_ramp_offset)
            gdb.execute('set openxr_frame.devices[0].matrix[2][3] = %.9g' % expected_hmd[2][3],
                        to_string=True)
        head = [expected_hmd[r][3] for r in range(3)]
        lines = []
        for hand in range(2):
            dev = hand + 1
            pos = (head[0] + (-0.24 if hand == 0 else 0.24), head[1] - 0.32, head[2] - 0.32)
            lines += [
                'set openxr_frame.devices[%d].valid = 1' % dev,
                'set openxr_frame.devices[%d].tracked = 1' % dev,
                'set openxr_frame.devices[%d].connected = 1' % dev,
                'set openxr_frame.devices[%d].kind = VRXR_DEVICE_HAND' % dev,
                'set openxr_frame.devices[%d].hand = %d' % (dev, hand),
                'set openxr_frame.hands[%d].active = 1' % hand,
                'set openxr_frame.hands[%d].profile = VRXR_PROFILE_INDEX' % hand,
                'set openxr_frame.hands[%d].pressed = %s' %
                    (hand, 'VRXR_BUTTON_PRIMARY' if expect_selected_prediction and
                     phase == 'fire' and hand == 0 and
                     not globals().get('lifecycle_stationary_fire', False) else '0'),
                'set openxr_frame.hands[%d].trigger = %s' % (hand, '0.90' if phase == 'fire' and hand == 1 else '0'),
                'set openxr_frame.hands[%d].grip = 0' % hand]
            for r in range(3):
                for c in range(3):
                    lines.append('set openxr_frame.devices[%d].matrix[%d][%d] = %d' % (dev,r,c,int(r == c)))
                lines.append('set openxr_frame.devices[%d].matrix[%d][3] = %.8g' % (dev,r,pos[r]))
            for axis in range(2):
                lines += ['set openxr_frame.hands[%d].stick[%d] = 0' % (hand,axis),
                          'set openxr_frame.hands[%d].pad[%d] = 0' % (hand,axis)]
        gdb.execute('\n'.join(lines), to_string=True)
        observed_hmd = [[fv('openxr_frame.devices[0].matrix[%d][%d]' % (r,c)) for c in range(4)] for r in range(3)]
        if any(float32(observed_hmd[r][c]) != expected_hmd[r][c]
               for r in range(3) for c in range(4)):
            if head_ramp_rate == 0 and head_z_ramp_rate is None:
                raise RuntimeError('controller injection changed the runtime HMD pose')
            raise RuntimeError('HMD pose did not match exact float32 readback')
        actions += 1
        if ramp_steps:
            result['head_ramp']['injected_action_samples'] += 1
            result['head_ramp']['last_action_sample'] = action_sample
            result['head_ramp']['last_offset_m'] = ramp_offset
            result['head_ramp']['last_written_hmd_x_m'] = expected_hmd[0][3]
        if head_z_ramp_rate is not None:
            result['head_z_ramp']['injected_action_samples'] += 1
            result['head_z_ramp']['last_action_sample'] = action_sample
            result['head_z_ramp']['last_offset_m'] = z_ramp_offset
            result['head_z_ramp']['anchor_hmd_z_m'] = head_z_anchor
            result['head_z_ramp']['last_written_hmd_z_m'] = expected_hmd[2][3]
        if phase == 'neutral':
            neutral_actions += 1
        else:
            # Observe the input adapter's real neutral/identity gate; do not
            # edit its state. Role 1/profile 2 are right hand/Index.
            armed = bool(iv('vr_input_hands[1].identity_valid') and
                         not iv('vr_input_hands[1].wait_neutral') and
                         iv('vr_input_hands[1].role') == 1 and
                         iv('vr_input_hands[1].profile') == 2)
            if not armed: raise RuntimeError('right Index hand is not neutral-rearmed')
    except Exception as exc:
        result['input_error'] = str(exc)
        return True
    return False

class Ready(gdb.Breakpoint):
    def stop(self):
        if time.monotonic() - started > 60: return True
        try:
            return (iv('cls.signon') == 4 and iv('cl.protocol_qsvr') == 1 and
                    iv('vulkan_globals.stereo_active') == 1 and iv('openxr_frame.should_render') == 1 and
                    iv('openxr_frame.focused') == 1 and iv('openxr_frame.devices[0].valid') == 1)
        except gdb.error: return False

class Frame(gdb.Breakpoint):
    def stop(self):
        global last_render_framecount
        if time.monotonic() - started > 105:
            result['error'] = '105-second overall deadline expired'
            return True
        if expect_selected_prediction:
            try:
                framecount = iv('host_framecount')
                render_completed = (last_render_framecount is None or
                                    framecount != last_render_framecount)
                completed_framecount = framecount - 1 if render_completed else None
                last_render_framecount = framecount
                if phase == 'fire' and render_completed:
                    observe_prediction_frame(completed_framecount)
            except Exception as exc:
                result['error'] = 'selected prediction frame: ' + str(exc)
                return True
        return True

class ReplayFinish(gdb.FinishBreakpoint):
    def __init__(self, frame, framecount, owner):
        super(ReplayFinish, self).__init__(frame, internal=True)
        self.framecount = framecount
        self.owner = owner

    def stop(self):
        global last_replay_result
        record = dict(framecount=self.framecount, owner=self.owner, success=False)
        prediction_probe['replay_returns'] += 1
        try:
            returned = self.return_value
            record['success'] = returned is not None and int(returned) != 0
            if record['success']:
                prediction_probe['replay_success'] += 1
        except Exception as exc:
            record['capture_error'] = str(exc)
            prediction_probe['return_capture_errors'] += 1
        last_replay_result = record
        return False

class ReplayCall(gdb.Breakpoint):
    def stop(self):
        global last_replay_result
        if phase != 'fire':
            return False
        framecount = None
        try:
            frame = gdb.newest_frame()
            framecount = iv('host_framecount')
            owner = iv('cl.viewentity')
            ReplayFinish(frame, framecount, owner)
        except Exception as exc:
            last_replay_result = dict(framecount=framecount, capture_error=str(exc))
            prediction_probe['call_probe_errors'] += 1
        return False

def prediction_sample():
    owner = iv('cl.viewentity')
    return dict(owner=owner,
        realtime=fv('realtime'), client_time=fv('cl.time'),
        frame_seconds=fv('host_frametime'), velocity=vec('cl.velocity'),
        pending_forward=fv('cl.pendingcmd.forwardmove'),
        origin=vec('cl.entities[%d].netstate.origin' % owner),
        displayed=vec('cl.entities[%d].origin' % owner),
        ack=iv('cl.ackedmovemessages'), sent=iv('cl.movemessages'),
        permission=bool(iv('cl.move_ack_prediction_allowed')),
        authority=iv('cl.move_ack_authority'),
        snapshot_valid=bool(iv('cl.move_snapshot_valid')),
        snapshot_ack=iv('cl.move_snapshot_ack'),
        snapshot_owner=iv('cl.move_snapshot_owner'))

def require_selected_prediction_state(state):
    if state['owner'] <= 0 or not (finite3(state['origin']) and finite3(state['displayed'])):
        raise RuntimeError('nonfinite selected owner pose')
    if not state['permission']:
        raise RuntimeError('selected prediction permission is disabled')
    if state['authority'] != 2:
        raise RuntimeError('selected prediction authority is not engine PMove (2)')
    if not state['snapshot_valid'] or state['snapshot_ack'] != state['ack'] or \
            state['snapshot_owner'] != state['owner']:
        raise RuntimeError('selected owner snapshot is not coherent with ACK/viewentity')

def observe_prediction_frame(completed_framecount):
    global prediction_previous, prediction_between_send
    current = prediction_sample()
    require_selected_prediction_state(current)
    prediction_probe['rendered_frames'] += 1
    replay = last_replay_result
    if replay is not None and replay.get('framecount') == completed_framecount:
        prediction_probe['frames_with_matching_return'] += 1
        if not replay.get('capture_error') and replay.get('success'):
            prediction_probe['matching_successful_returns'] += 1
            replay_proven = replay.get('owner') == current['owner']
            if replay_proven:
                prediction_probe['matching_owner_returns'] += 1
        else:
            replay_proven = False
    else:
        replay_proven = False
    current['replay_proven'] = replay_proven
    if prediction_previous is not None and prediction_between_send is None:
        unchanged = (current['ack'] == prediction_previous['ack'] and
                     current['origin'] == prediction_previous['origin'] and
                     current['sent'] == prediction_previous['sent'] and
                     current['owner'] == prediction_previous['owner'] and
                     current['permission'] and prediction_previous['permission'] and
                     current['authority'] == prediction_previous['authority'])
        if unchanged:
            prediction_probe['unchanged_ack_sent_authority_pairs'] += 1
            displacement = distance3(prediction_previous['displayed'], current['displayed'])
            if not math.isfinite(displacement):
                raise RuntimeError('nonfinite displayed-owner displacement')
            if displacement >= prediction_probe['max_stable_candidate_displacement']:
                prediction_probe['largest_candidate_pair'] = dict(
                    previous=prediction_previous, current=current,
                    elapsed_seconds=current['realtime']-prediction_previous['realtime'],
                    displacement=displacement)
            prediction_probe['max_stable_candidate_displacement'] = round(max(
                prediction_probe['max_stable_candidate_displacement'], displacement), 3)
            replay_pair_proven = current['replay_proven'] and prediction_previous['replay_proven']
            if replay_pair_proven:
                prediction_probe['replay_proven_stable_pairs'] += 1
                prediction_probe['max_replay_proven_stable_displacement'] = round(max(
                    prediction_probe['max_replay_proven_stable_displacement'], displacement), 3)
            if replay_pair_proven and displacement >= 0.25:
                prediction_between_send = dict(
                    ack=current['ack'], sent=current['sent'],
                    authoritative_origin=current['origin'],
                    from_displayed=prediction_previous['displayed'],
                    to_displayed=current['displayed'], displacement=round(displacement, 3))
    prediction_previous = current

def prediction_failure_cause():
    if not prediction_probe['replay_returns']:
        return 'no_production_replay_returns_observed'
    if not prediction_probe['frames_with_matching_return']:
        return 'no_replay_return_matched_a_rendered_frame'
    if not prediction_probe['unchanged_ack_sent_authority_pairs']:
        return 'no_unchanged_ack_sent_authority_frames'
    if not prediction_probe['replay_proven_stable_pairs']:
        return 'stable_frames_lacked_two_successful_owner_matched_replays'
    return 'replay_proven_displacement_below_0.25'

class Actions(gdb.Breakpoint):
    def stop(self):
        if lifecycle_hook is not None and lifecycle_hook.skip_input(): return False
        return inject()

class PrivateWire(gdb.Breakpoint):
    def stop(self):
        global fire_prediction_command
        try:
            if not iv('cmd') or not iv('cmd->vr_active') or not iv('cmd->vr_handpos_relative'):
                return False
            seq = iv('cmd->sequence')
            rec = {'sequence':seq, 'vr_active':True, 'handpos_relative':True,
                   'buttons':iv('cmd->buttons'), 'attack':bool(iv('cmd->buttons') & 1),
                   'relative_muzzle':vec('cmd->vr_handpos'),
                   'hand_angles':vec('cmd->vr_handrot'),
                   'roomscale':vec('cmd->vr_roomscalemove')}
            if expect_selected_prediction:
                rec['forwardmove'] = iv('cmd->forwardmove')
                rec['seconds'] = fv('cmd->seconds')
                rec['msec'] = iv('cmd->msec')
                rec['servertime'] = fv('cmd->servertime')
            rec['finite'] = all(finite3(rec[k]) for k in ('relative_muzzle','hand_angles','roomscale'))
            if (expect_selected_prediction and fire_prediction_command is None and
                    phase == 'fire' and rec['attack'] and rec['forwardmove'] > 0 and
                    rec['finite']):
                fire_prediction_command = rec
            wire[seq] = rec
            if lifecycle_hook is not None: lifecycle_hook.observe_wire(rec)
        except Exception as exc:
            result['wire_error'] = str(exc)
        return False

ready = Ready('Host_Frame', internal=True)
fatal = [gdb.Breakpoint('Host_Error', internal=True), gdb.Breakpoint('Sys_Error', internal=True)]

def save():
    if result_path:
        with open(result_path, 'w') as f: json.dump(result, f, indent=2, sort_keys=True)

def continue_frame():
    gdb.execute('continue')
    name = gdb.newest_frame().name()
    if name == 'Host_Frame': return
    if name == 'VR_InputCommands': raise RuntimeError(result.get('input_error','controller injection stopped'))
    raise RuntimeError('inferior stopped in ' + name)

def run_actions(count):
    target = actions + count
    while actions < target:
        continue_frame()
        if result.get('error'): raise RuntimeError(result['error'])

try:
    if not result_path: raise RuntimeError('QSVR_PINNED_VR_RESULT is required')
    if (head_ramp_rate is None or not math.isfinite(head_ramp_rate) or
            head_ramp_rate < 0.0 or head_ramp_rate > 0.1):
        raise RuntimeError('QSVR_PINNED_VR_HEAD_RAMP_METERS_PER_ACTION must be 0 or a finite positive value at most 0.1')
    if (head_z_ramp_text is not None and
            (head_z_ramp_rate is None or not math.isfinite(head_z_ramp_rate) or
             abs(head_z_ramp_rate) > 0.1)):
        raise RuntimeError('QSVR_PINNED_VR_HEAD_Z_METERS_PER_ACTION must be a finite value with magnitude at most 0.1')
    result['head_ramp'] = {'enabled':head_ramp_rate > 0.0,
        'meters_per_action':head_ramp_rate, 'starts_after_action_samples':25,
        'injected_action_samples':0}
    result['head_z_ramp'] = {'enabled':head_z_ramp_rate is not None,
        'synthetic':head_z_ramp_rate is not None, 'meters_per_action':head_z_ramp_rate,
        'starts_after_action_samples':25, 'injected_action_samples':0}
    gdb.execute('run')
    if gdb.newest_frame().name() != 'Host_Frame' or not (
        iv('cls.signon') == 4 and iv('cl.protocol_qsvr') == 1 and
        iv('vulkan_globals.stereo_active') == 1 and iv('openxr_frame.focused') and
        iv('openxr_frame.devices[0].valid')):
        raise RuntimeError('focused OpenXR stereo/private signon was not reached')
    result['admission'] = {'signon':iv('cls.signon'), 'private_protocol':iv('cl.protocol_qsvr'),
        'stereo':bool(iv('vulkan_globals.stereo_active')), 'focused':bool(iv('openxr_frame.focused')),
        'runtime_hmd_valid':bool(iv('openxr_frame.devices[0].valid')),
        'hmd_injection':('none; runtime-owned device 0 is checked unchanged at every action sample'
            if head_ramp_rate == 0.0 and head_z_ramp_rate is None else
            'controlled completed-frame HMD adjustment; every HMD matrix component is checked at exact float32 precision')}
    gdb.execute('delete breakpoints', to_string=True)
    Frame('Host_Frame', internal=True)
    Actions('VR_InputCommands', internal=True)
    PrivateWire('CL_WritePrivateUsercmd', internal=True)
    if expect_selected_prediction:
        ReplayCall('CL_ComputeReplayPlayerMovement', internal=True)
    fatal = [gdb.Breakpoint('Host_Error', internal=True), gdb.Breakpoint('Sys_Error', internal=True)]
    bindings = 'bind RTRIGGER +attack\n'
    if expect_selected_prediction:
        bindings += 'bind ABUTTON +forward\n'
    cbuf(bindings + 'vr_aimmode 7\nvr_lefthanded 0\nvr_movement_mode 0\nimpulse 2\n')
    if abs(fv('Cvar_VariableValue("vr_aimmode")') - 7.0) > 0.01:
        raise RuntimeError('controller aim mode did not activate')

    # Native neutral XR samples arm the synthetic right Index hand. Wait for
    # impulse 2 and the active id1 schema to select/calibrate v_shot.mdl.
    run_actions(18)
    result['neutral_action_samples'] = 18
    gdb.execute('set $vr_probe_muzzle = (float *)calloc(3, sizeof(float))', to_string=True)
    muzzle = None
    for _ in range(180):
        index = iv('cl.stats[STAT_WEAPON]')
        name = None
        if 0 < index < iv('sizeof(cl.model_precache) / sizeof(cl.model_precache[0])'):
            model = gdb.parse_and_eval('cl.model_precache[%d]' % index)
            if int(model): name = model.dereference()['name'].string()
        valid = iv('(int)VR_WeaponCalibrationCurrentMuzzle((float *)$vr_probe_muzzle)')
        candidate = vec('$vr_probe_muzzle')
        if valid and name == 'progs/v_shot.mdl':
            muzzle = candidate
            break
        run_actions(1)
    if muzzle is None: raise RuntimeError('active schema did not resolve vanilla shotgun muzzle')
    if not finite3(muzzle) or math.sqrt(sum(x*x for x in muzzle)) < 1.0:
        raise RuntimeError('vanilla shotgun calibration is missing or unusable')
    result['calibration'] = {'model':'progs/v_shot.mdl', 'muzzle_offset':muzzle,
                             'active_game_schema_resolved':True}
    shells_before = iv('cl.stats[STAT_SHELLS]')
    if shells_before <= 0: raise RuntimeError('server reports no shotgun shells')
    packets_before = iv('cl.net_move_packets_sent')
    cmds_before = iv('cl.net_move_cmds_sent')

    phase = 'fire'
    run_actions(90)
    phase = 'neutral'
    run_actions(24)
    shells_after = iv('cl.stats[STAT_SHELLS]')
    ack = iv('cl.ackedmovemessages')
    acknowledged_attacks = [seq for seq, rec in wire.items() if rec['attack'] and seq <= ack]
    if not armed: raise RuntimeError('right trigger was not delivered to a rearmed hand')
    if not any(rec['attack'] and rec['finite'] for rec in wire.values()):
        raise RuntimeError('no finite serialized private VR attack command was observed')
    if not acknowledged_attacks:
        raise RuntimeError('pinned server ACK did not cover a serialized private VR attack command')
    if iv('cl.net_move_packets_sent') <= packets_before or iv('cl.net_move_cmds_sent') <= cmds_before:
        raise RuntimeError('client transport did not report a successfully sent movement packet')
    if shells_after >= shells_before:
        raise RuntimeError('server shell count did not decrease while the XR trigger was held')
    prediction_evidence = None
    if expect_selected_prediction:
        movement_command = fire_prediction_command
        if movement_command is None:
            raise RuntimeError('no firing-phase serialized VR command carried +forward movement')
        if prediction_between_send is None:
            prediction_probe['failure_cause'] = prediction_failure_cause()
            raise RuntimeError('no replay-proven displayed-owner movement between server sends')
        prediction_final = prediction_sample()
        require_selected_prediction_state(prediction_final)
        if prediction_final['ack'] < movement_command['sequence']:
            raise RuntimeError('selected VR movement command was not acknowledged')
        settled_error = distance3(prediction_final['displayed'], prediction_final['origin'])
        if not math.isfinite(settled_error) or settled_error > 16.0:
            raise RuntimeError('displayed selected owner did not converge to authority')
        prediction_evidence = dict(
            permission=prediction_final['permission'], authority=prediction_final['authority'],
            owner=prediction_final['owner'], snapshot_ack=prediction_final['snapshot_ack'],
            snapshot_owner=prediction_final['snapshot_owner'],
            synthetic_input='OpenXR left primary -> ABUTTON +forward during fire',
            serialized_vr_forward_attack=dict(sequence=movement_command['sequence'],
                forwardmove=movement_command['forwardmove'], attack=movement_command['attack'],
                vr_active=movement_command['vr_active'],
                handpos_relative=movement_command['handpos_relative']),
            replay_probe=prediction_probe, between_send=prediction_between_send,
            settled=dict(ack=prediction_final['ack'], sent=prediction_final['sent'],
                         owner_error=round(settled_error, 3)))
    seq = max(acknowledged_attacks)
    result.update({'status':'passed', 'input':{'samples':actions, 'neutral_samples':18,
        'right_hand_rearmed':armed, 'native_trigger':'right OpenXR action value 0.90'},
        'private_command':{'wire_command':wire[seq], 'server_ack_sequence':ack,
        'transport_packets_sent':iv('cl.net_move_packets_sent')-packets_before,
        'transport_commands_sent':iv('cl.net_move_cmds_sent')-cmds_before,
        'acknowledged_private_attack_sequence':seq},
        'firing':{'shells_before':shells_before, 'shells_after':shells_after,
                  'shells_consumed':shells_before-shells_after}})
    if prediction_evidence is not None:
        result['selected_prediction'] = prediction_evidence
except Exception as exc:
    result['status'] = 'failed'
    result['error'] = str(exc)
    result['actions_seen'] = actions
    result['serialized_private_commands'] = list(wire.values())[-12:]
    if expect_selected_prediction:
        if prediction_between_send is None and 'failure_cause' not in prediction_probe:
            prediction_probe['failure_cause'] = prediction_failure_cause()
        result['prediction_probe'] = prediction_probe
finally:
    try: save()
    except Exception as exc: gdb.write('QSVR_PINNED_VR_RESULT_WRITE_FAILED: %s\n' % exc)

marker = 'QSVR_PINNED_VR_PASSED ' if result['status'] == 'passed' else 'QSVR_PINNED_VR_FAILED '
gdb.write(marker + json.dumps(result, separators=(',', ':')) + '\n')
if result['status'] != 'passed': raise RuntimeError(result.get('error','probe failed'))
if os.environ.get('QSVR_LIFECYCLE_ROOT'):
    helper = os.environ.get('QSVR_LIFECYCLE_HELPER')
    if not helper: raise RuntimeError('QSVR_LIFECYCLE_HELPER is required with lifecycle mode')
    exec(compile(open(helper).read(), helper, 'exec'), globals())
    ConnectedLifecycle(globals()).run()
end
quit
