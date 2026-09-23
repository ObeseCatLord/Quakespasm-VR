# Focused live OpenXR/private-peer smoke. Caller supplies runtime environment,
# basedir, and client arguments (including +connect <address:port> qsvr1).
# Only synthetic controller poses/actions are injected; the real HMD/runtime,
# input/key binding, command builder, transport, pinned server, and PMove run.
# No usercmd or server state is written. Geometry and roomscale qualification
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
import gdb, json, math, os, time

started = time.monotonic()
phase = 'neutral'
actions = 0
neutral_actions = 0
armed = False
wire = {}
result = {'status':'running', 'scope':'focused XR admission, private VR command, shotgun shell consumption',
          'follow_up':['body/eye/muzzle geometry', 'roomscale movement and collision',
                       'weapon damage/effects and physical headset/controller qualification']}
result_path = os.environ.get('QSVR_PINNED_VR_RESULT')

def iv(expr): return int(gdb.parse_and_eval(expr))
def fv(expr): return float(gdb.parse_and_eval(expr))
def vec(expr): return [fv('%s[%d]' % (expr, i)) for i in range(3)]
def finite3(v): return len(v) == 3 and all(math.isfinite(x) for x in v)
def cbuf(text):
    gdb.execute('call (void)Cbuf_AddText(' + json.dumps(text) + ')', to_string=True)
    gdb.execute('call (void)Cbuf_Execute()', to_string=True)

def inject():
    global actions, neutral_actions, armed
    try:
        if int(gdb.parse_and_eval('(unsigned long)frame')) != int(gdb.parse_and_eval('(unsigned long)&openxr_frame')):
            raise RuntimeError('input did not receive the live OpenXR frame')
        if not (iv('vulkan_globals.stereo_active') and iv('openxr_frame.should_render') and
                iv('openxr_frame.focused') and iv('openxr_frame.devices[0].valid')):
            raise RuntimeError('focused stereo/HMD admission was lost')
        hmd = [[fv('openxr_frame.devices[0].matrix[%d][%d]' % (r,c)) for c in range(4)] for r in range(3)]
        head = [hmd[r][3] for r in range(3)]
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
                'set openxr_frame.hands[%d].pressed = 0' % hand,
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
        if hmd != [[fv('openxr_frame.devices[0].matrix[%d][%d]' % (r,c)) for c in range(4)] for r in range(3)]:
            raise RuntimeError('controller injection changed the runtime HMD pose')
        actions += 1
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
        if time.monotonic() - started > 105:
            result['error'] = '105-second overall deadline expired'
        return True

class Actions(gdb.Breakpoint):
    def stop(self): return inject()

class PrivateWire(gdb.Breakpoint):
    def stop(self):
        try:
            if not iv('cmd') or not iv('cmd->vr_active') or not iv('cmd->vr_handpos_relative'):
                return False
            seq = iv('cmd->sequence')
            rec = {'sequence':seq, 'vr_active':True, 'handpos_relative':True,
                   'buttons':iv('cmd->buttons'), 'attack':bool(iv('cmd->buttons') & 1),
                   'relative_muzzle':vec('cmd->vr_handpos'),
                   'hand_angles':vec('cmd->vr_handrot'),
                   'roomscale':vec('cmd->vr_roomscalemove')}
            rec['finite'] = all(finite3(rec[k]) for k in ('relative_muzzle','hand_angles','roomscale'))
            wire[seq] = rec
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
    gdb.execute('run')
    if gdb.newest_frame().name() != 'Host_Frame' or not (
        iv('cls.signon') == 4 and iv('cl.protocol_qsvr') == 1 and
        iv('vulkan_globals.stereo_active') == 1 and iv('openxr_frame.focused') and
        iv('openxr_frame.devices[0].valid')):
        raise RuntimeError('focused OpenXR stereo/private signon was not reached')
    result['admission'] = {'signon':iv('cls.signon'), 'private_protocol':iv('cl.protocol_qsvr'),
        'stereo':bool(iv('vulkan_globals.stereo_active')), 'focused':bool(iv('openxr_frame.focused')),
        'runtime_hmd_valid':bool(iv('openxr_frame.devices[0].valid')),
        'hmd_injection':'none; runtime-owned device 0 is checked unchanged at every action sample'}
    gdb.execute('delete breakpoints', to_string=True)
    Frame('Host_Frame', internal=True)
    Actions('VR_InputCommands', internal=True)
    PrivateWire('CL_WritePrivateUsercmd', internal=True)
    fatal = [gdb.Breakpoint('Host_Error', internal=True), gdb.Breakpoint('Sys_Error', internal=True)]
    cbuf('bind RTRIGGER +attack\nvr_aimmode 7\nvr_lefthanded 0\nvr_movement_mode 0\nimpulse 2\n')
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
    seq = max(acknowledged_attacks)
    result.update({'status':'passed', 'input':{'samples':actions, 'neutral_samples':18,
        'right_hand_rearmed':armed, 'native_trigger':'right OpenXR action value 0.90'},
        'private_command':{'wire_command':wire[seq], 'server_ack_sequence':ack,
        'transport_packets_sent':iv('cl.net_move_packets_sent')-packets_before,
        'transport_commands_sent':iv('cl.net_move_cmds_sent')-cmds_before,
        'acknowledged_private_attack_sequence':seq},
        'firing':{'shells_before':shells_before, 'shells_after':shells_after,
                  'shells_consumed':shells_before-shells_after}})
except Exception as exc:
    result['status'] = 'failed'
    result['error'] = str(exc)
    result['actions_seen'] = actions
    result['serialized_private_commands'] = list(wire.values())[-12:]
finally:
    try: save()
    except Exception as exc: gdb.write('QSVR_PINNED_VR_RESULT_WRITE_FAILED: %s\n' % exc)

marker = 'QSVR_PINNED_VR_PASSED ' if result['status'] == 'passed' else 'QSVR_PINNED_VR_FAILED '
gdb.write(marker + json.dumps(result, separators=(',', ':')) + '\n')
if result['status'] != 'passed': raise RuntimeError(result.get('error','probe failed'))
end
quit
