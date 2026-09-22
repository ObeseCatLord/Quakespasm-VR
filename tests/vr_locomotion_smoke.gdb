# Initialized gameplay proof for the native locomotion port. Requires a debug
# build, a disposable basedir with stock id1 assets, and an already running
# simulated Monado service. Controllers are injected at the completed-frame
# boundary because this simulator provides only an HMD. The script never
# starts/stops the runtime, forces stereo, writes a usercmd/kbutton, changes
# collision state, or calls production movement helpers directly.
#
# From the repository root, with the same private XR environment as
# tests/openxr-local-smoke.gdb:
# QSVR_LOCOMOTION_RESULT=/tmp/qsvr-locomotion.json \
#   timeout --signal=TERM 180s gdb -nx --batch \
#   -x tests/vr_locomotion_smoke.gdb --args "$QSVR_LOCOMOTION_BINARY" \
#   -openxr -nosound -window -width 640 -height 480 \
#   -basedir "$QSVR_LOCOMOTION_BASEDIR" +map e1m1
# Keep the runtime manifest, XDG runtime/config/data directories and
# validation-layer settings isolated as described in tests/README.md.

set pagination off
set confirm off
set debuginfod enabled off
set print thread-events off
python
import gdb, json, math, os, statistics, time

started = time.monotonic()
stage = 'neutral'
frame_budget = None
preview_due = False
preview_done = False
command_samples = {}
view_samples = {}
input_counts = {}
pose_counts = {}
last_host_frame = None
last_rendered_yaw = None
timing_records = []
timing_preview_due = False
timing_preview_requested = False

def iv(expr):
    return int(gdb.parse_and_eval(expr))

def fv(expr):
    return float(gdb.parse_and_eval(expr))

def vector(expr):
    return [fv('%s[%d]' % (expr, i)) for i in range(3)]

def angle_delta(after, before):
    return (after - before + 180.0) % 360.0 - 180.0

def circular_mean(values):
    s = sum(math.sin(math.radians(v)) for v in values)
    c = sum(math.cos(math.radians(v)) for v in values)
    return math.degrees(math.atan2(s, c)) % 360.0

def median_command(name, field):
    samples = command_samples.get(name, [])
    assert len(samples) >= 8, (name, len(samples))
    return statistics.median(s[field] for s in samples[-10:])

def player_origin():
    return vector('cl.entities[cl.viewentity].netstate.origin')

def player_view_yaw():
    assert last_rendered_yaw is not None, 'no adjusted stereo render camera captured yet'
    return last_rendered_yaw

def distance(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))

def cbuf(text):
    # All inferior calls are made by the top-level driver at a Host_Frame
    # boundary, never from a Breakpoint.stop callback.
    gdb.execute('call (void)Cbuf_AddText(' + json.dumps(text) + ')', to_string=True)
    gdb.execute('call (void)Cbuf_Execute()', to_string=True)

def set_cvars(**values):
    cbuf(''.join('%s %s\n' % (name, value) for name, value in values.items()))

def cvar(name):
    return float(gdb.parse_and_eval('Cvar_VariableValue(' + json.dumps(name) + ')'))

def matrix_for_pose(yaw, pitch=0.0, roll=0.0):
    # OpenXR-space rotation only. Convert desired Quake yaw/pitch/roll to a
    # quaternion, then to a 3x3 matrix; preserve each live device translation.
    def mul(a, b):
        aw, ax, ay, az = a
        bw, bx, by, bz = b
        return (aw*bw-ax*bx-ay*by-az*bz,
                aw*bx+ax*bw+ay*bz-az*by,
                aw*by-ax*bz+ay*bw+az*bx,
                aw*bz+ax*by-ay*bx+az*bw)
    hy, hp, hr = (math.radians(v) * 0.5 for v in (yaw, pitch, roll))
    qy = (math.cos(hy), 0.0, math.sin(hy), 0.0)
    qx = (math.cos(hp), -math.sin(hp), 0.0, 0.0)
    qz = (math.cos(hr), 0.0, 0.0, -math.sin(hr))
    w, x, y, z = mul(mul(qy, qx), qz)
    return [[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
            [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
            [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]]

pose = dict(head=(0.0, 0.0, 0.0), left=(72.0, -5.0, 0.0),
            right=(-53.0, 8.0, 0.0))

# Axis values are injected into the completed runtime action sample. Set both
# stick and pad because the real adapter maps those by the runtime profile.
plans = {
    'neutral':       dict(focus=1, left=(0.0, 0.0), right=(0.0, 0.0)),
    'timing':        dict(focus=1, left=(0.0, 0.5), right=(0.0, 0.0)),
    'timing_catchup': dict(focus=1, left=(0.0, 0.5), right=(0.0, 0.0)),
    'move_head':     dict(focus=1, left=(0.22, 0.68), right=(0.22, 0.68)),
    'move_hand_rd':  dict(focus=1, left=(0.22, 0.68), right=(0.22, 0.68)),
    'move_raw_rd':   dict(focus=1, left=(0.22, 0.68), right=(0.22, 0.68)),
    'move_hand_ld':  dict(focus=1, left=(0.22, 0.68), right=(0.22, 0.68)),
    'move_raw_ld':   dict(focus=1, left=(0.22, 0.68), right=(0.22, 0.68)),
    'focus_lost':    dict(focus=0, left=(0.0, 0.78), right=(0.0, 0.78)),
    'focus_held':    dict(focus=1, left=(0.0, 0.78), right=(0.0, 0.78)),
    'rearm_neutral': dict(focus=1, left=(0.0, 0.0), right=(0.0, 0.0)),
    'focus_rearmed': dict(focus=1, left=(0.0, 0.78), right=(0.0, 0.78)),
    'desktop_base':  dict(focus=1, left=(0.0, 0.0), right=(0.0, 0.0)),
    'vr_mix':        dict(focus=1, left=(0.0, 0.50), right=(0.0, 0.50)),
    'vr_speed_mix':  dict(focus=1, left=(0.0, 0.50), right=(0.0, 0.50)),
    'vr_speed_only': dict(focus=1, left=(0.0, 0.50), right=(0.0, 0.50)),
    'turn_neutral':  dict(focus=1, left=(0.0, 0.0), right=(0.0, 0.0)),
    'snap_positive': dict(focus=1, left=(0.0, 0.0), right=(0.82, 0.0)),
    'snap_reverse':  dict(focus=1, left=(0.0, 0.0), right=(-0.82, 0.0)),
    'turn_180':      dict(focus=1, left=(0.0, 0.0), right=(0.0, 0.0), right_active=0),
    'turn_smooth':   dict(focus=1, left=(0.0, 0.0), right=(0.68, 0.0)),
}

def write_pose(device, spec):
    mat = matrix_for_pose(*spec)
    commands = []
    for row in range(3):
        for col in range(3):
            commands.append('set openxr_frame.devices[%d].matrix[%d][%d] = %.9g' %
                            (device, row, col, mat[row][col]))
    gdb.execute('\n'.join(commands), to_string=True)

def write_synthetic_controllers(plan):
    commands = []
    for hand in range(2):
        active = plan.get('right_active', 1) if hand == 1 else 1
        commands.extend([
            'set openxr_frame.devices[%d].valid = 1' % (hand + 1),
            'set openxr_frame.devices[%d].tracked = 1' % (hand + 1),
            'set openxr_frame.devices[%d].connected = 1' % (hand + 1),
            'set openxr_frame.devices[%d].kind = VRXR_DEVICE_HAND' % (hand + 1),
            'set openxr_frame.devices[%d].hand = %d' % (hand + 1, hand),
            'set openxr_frame.hands[%d].active = %d' % (hand, active),
            'set openxr_frame.hands[%d].profile = VRXR_PROFILE_INDEX' % hand])
    gdb.execute('\n'.join(commands), to_string=True)

class Ready(gdb.Breakpoint):
    def stop(self):
        if time.monotonic() - started > 60:
            return True
        try:
            return (iv('cls.signon') == 4 and iv('vulkan_globals.stereo_active') == 1 and
                    iv('openxr_frame.should_render') == 1 and iv('openxr_frame.focused') == 1 and
                    iv('openxr_frame.devices[0].valid') == 1)
        except gdb.error:
            return False

ready = Ready('Host_Frame', internal=True)
gdb.Breakpoint('Host_Error', internal=True)
gdb.Breakpoint('Sys_Error', internal=True)
gdb.execute('run')
assert (gdb.newest_frame().name() == 'Host_Frame' and iv('cls.signon') == 4 and
        iv('vulkan_globals.stereo_active') == 1 and iv('openxr_frame.should_render') == 1 and
        iv('openxr_frame.focused') == 1), 'did not reach focused, initialized stereo gameplay'
assert iv('openxr_frame.devices[0].valid') == 1, 'real runtime HMD pose unavailable'
ready.delete()

# The actual frame has already passed GL_OpenXRFrame and the renderer's stereo
# admission. These breakpoints alter only the completed pose/action boundary.
class PoseBoundary(gdb.Breakpoint):
    def stop(self):
        if iv('vulkan_globals.stereo_active') != 1:
            return False
        pose_counts[stage] = pose_counts.get(stage, 0) + 1
        write_synthetic_controllers(plans[stage])
        write_pose(0, pose['head'])
        write_pose(1, pose['left'])
        write_pose(2, pose['right'])
        return False

class ActionBoundary(gdb.Breakpoint):
    def stop(self):
        # VR_InputCommands must receive GL_OpenXRFrame's real object. Do not
        # substitute a test frame or touch key/button ownership state.
        frame_address = int(gdb.parse_and_eval('(unsigned long)frame'))
        actual_address = int(gdb.parse_and_eval('(unsigned long)&openxr_frame'))
        assert frame_address == actual_address, 'input did not receive the live OpenXR frame'
        assert iv('vulkan_globals.stereo_active') == 1
        plan = plans[stage]
        write_synthetic_controllers(plan)
        commands = ['set openxr_frame.focused = %d' % plan['focus']]
        for hand in range(2):
            x, y = plan['left' if hand == 0 else 'right']
            commands.extend([
                'set openxr_frame.hands[%d].stick[0] = %.7g' % (hand, x),
                'set openxr_frame.hands[%d].stick[1] = %.7g' % (hand, y),
                'set openxr_frame.hands[%d].pad[0] = %.7g' % (hand, x),
                'set openxr_frame.hands[%d].pad[1] = %.7g' % (hand, y),
                'set openxr_frame.hands[%d].pressed = 0' % hand,
                'set openxr_frame.hands[%d].trigger = 0' % hand])
        gdb.execute('\n'.join(commands), to_string=True)
        input_counts[stage] = input_counts.get(stage, 0) + 1
        return False

class FrameGate(gdb.Breakpoint):
    def stop(self):
        global frame_budget, last_host_frame, timing_preview_due, timing_preview_requested
        current = iv('host_framecount')
        if last_host_frame == current:
            return False
        last_host_frame = current
        if stage == 'timing':
            pending = bool(iv('cl.pendingcmd.vr_pending_move_valid'))
            timing_records.append(dict(host=current-1, pending=pending,
                angles=bool(iv('cl.pendingcmd.vr_pending_angles_valid')),
                forward=fv('cl.pendingcmd.vr_pending_move[0]')))
            if pending and not timing_preview_requested:
                timing_preview_due = timing_preview_requested = True
                return True
        if frame_budget is None:
            return False
        frame_budget -= 1
        return frame_budget <= 0

class RenderCamera(gdb.Breakpoint):
    def stop(self):
        global last_rendered_yaw
        if iv('stereo_view_adjusted') != 1:
            return False
        last_rendered_yaw = fv('r_refdef.viewangles[1]')
        if stage.startswith('turn_') or stage.startswith('snap_'):
            view_samples.setdefault(stage, []).append(last_rendered_yaw)
        return False

class SendBoundary(gdb.Breakpoint):
    def stop(self):
        global preview_due
        try:
            if int(gdb.parse_and_eval('cmd')) == 0:
                return False
            record = dict(host=iv('host_framecount'), forward=float(gdb.parse_and_eval('cmd->forwardmove')),
                          side=float(gdb.parse_and_eval('cmd->sidemove')),
                          up=float(gdb.parse_and_eval('cmd->upmove')),
                          angles=[float(gdb.parse_and_eval('cmd->viewangles[%d]' % i)) for i in range(3)])
        except gdb.error:
            raise RuntimeError('debug build must expose CL_SendMove usercmd parameters')
        command_samples.setdefault(stage, []).append(record)
        if preview_due:
            preview_due = False
            return True
        return False

pose_bp = PoseBoundary('V_UpdateTrackedAim', internal=True)
input_bp = ActionBoundary('VR_InputCommands', internal=True)
frame_bp = FrameGate('Host_Frame', internal=True)
camera_bp = RenderCamera('R_RestoreStereoView', internal=True)
send_bp = SendBoundary('CL_SendMove', internal=True)

def memory(expr):
    address = int(gdb.parse_and_eval('&' + expr))
    size = int(gdb.parse_and_eval('sizeof(' + expr + ')'))
    return bytes(gdb.selected_inferior().read_memory(address, size))

def readonly_snapshot():
    names = ('in_mlook', 'in_klook', 'in_left', 'in_right', 'in_forward', 'in_back',
             'in_lookup', 'in_lookdown', 'in_moveleft', 'in_moveright', 'in_strafe',
             'in_speed', 'in_up', 'in_down', 'in_use', 'in_jump', 'in_attack', 'in_impulse')
    return (memory('cl'), tuple(memory(name) for name in names))

def verify_preview():
    global preview_done
    before = readonly_snapshot()
    outputs = []
    for _ in range(5):
        gdb.execute('call (void)CL_PreviewMove($vr_locomotion_preview)', to_string=True)
        outputs.append(tuple(float(gdb.parse_and_eval('$vr_locomotion_preview->%s' % field))
                             for field in ('forwardmove', 'sidemove', 'upmove')))
        assert readonly_snapshot() == before, 'CL_PreviewMove changed client or native input state'
    assert all(value == outputs[0] for value in outputs), outputs
    assert abs(outputs[0][0]) > 20.0, outputs
    preview_done = True
    return dict(count=len(outputs), command=outputs[0], owners_unchanged=True)

def run_frames(count):
    global frame_budget, timing_preview_due
    frame_budget = count
    while True:
        gdb.execute('continue')
        name = gdb.newest_frame().name()
        if name == 'CL_SendMove':
            assert not preview_done, 'unexpected sender breakpoint'
            preview_record = verify_preview()
            results['preview'] = preview_record
            continue
        if name == 'Host_Frame' and timing_preview_due:
            timing_preview_due = False
            results['no_send_preview'] = verify_preview()
            continue
        if name != 'Host_Frame':
            raise RuntimeError('game stopped in %s during %s' % (name, stage))
        assert frame_budget == 0, ('frame budget ended early', frame_budget)
        break

def run_stage(name, frames):
    global stage
    stage = name
    origin_start = player_origin()
    run_frames(frames)
    origin_end = player_origin()
    results.setdefault('stages', {})[name] = dict(
        commands=len(command_samples.get(name, [])),
        input_samples=input_counts.get(name, 0), pose_samples=pose_counts.get(name, 0),
        origin_start=origin_start, origin_end=origin_end,
        origin_delta=distance(origin_start, origin_end))

def set_stage_cvars(name, mode, lefthanded):
    global stage
    stage = 'neutral'
    set_cvars(vr_movement_mode=mode, vr_lefthanded=lefthanded,
              vr_aimmode=7, vr_movement_speed=1, vr_snap_turn=0,
              vr_turn_speed=2, vr_joystick_yaw_multi=1, vr_180_snap_turn=1,
              cl_alwaysrun=0, cl_desktop_vanilla_run=1,
              cl_forwardspeed=200, cl_movespeedkey=2)
    for name_, wanted in (('vr_movement_mode', mode), ('vr_lefthanded', lefthanded),
                          ('vr_aimmode', 7), ('vr_movement_speed', 1)):
        assert abs(cvar(name_) - float(wanted)) < 0.01, name_
    run_stage('neutral', 12)

def useful(name, field):
    samples = command_samples.get(name, [])
    assert len(samples) >= 12, (name, len(samples))
    return [s[field] for s in samples[-10:]]

def command_yaw(name):
    return circular_mean([s['angles'][1] for s in command_samples[name][-10:]])

def path_delta(name, initial):
    values = [initial] + view_samples.get(name, [])
    return sum(angle_delta(b, a) for a, b in zip(values, values[1:]))

# Initial neutral lets the real input adapter establish its profile/role gate.
results = dict(stages={}, limits=[
    'No absolute/relative server-angle or tracking-origin rebase injection in this smoke.',
    'Pose/action values are injected at native XR boundaries; no physical controller qualification.',
    'The stock e1m1 local server, collision and command path run normally.'
])
set_cvars(vr_movement_mode=0, vr_lefthanded=0, vr_aimmode=7,
          vr_movement_speed=1, vr_snap_turn=0, vr_turn_speed=2,
          vr_joystick_yaw_multi=1, vr_180_snap_turn=1,
          cl_alwaysrun=0, cl_desktop_vanilla_run=1,
          cl_forwardspeed=200, cl_movespeedkey=2)
cbuf('unbind LEFTARROW\nunbind RIGHTARROW\n-forward 119\n-speed 120\n')
assert abs(cvar('vr_aimmode') - 7.0) < 0.01
gdb.execute('set $vr_locomotion_preview = (usercmd_t *)calloc(1, sizeof(usercmd_t))', to_string=True)
assert int(gdb.parse_and_eval('$vr_locomotion_preview')) != 0
run_stage('neutral', 24)
movement_origin_start = player_origin()

# Modes 0/1/2 use distinct live head, offhand and dominant-hand orientations.
# Handedness flips which actual pose is the offhand/dominant source. Neutral
# frames after each cvar change let production invalidation/rearm run normally.
mode_cases = [('move_head', 0, 0), ('move_hand_rd', 1, 0),
              ('move_raw_rd', 2, 0), ('move_hand_ld', 1, 1), ('move_raw_ld', 2, 1)]
for case, mode, lefthanded in mode_cases:
    set_stage_cvars(case, mode, lefthanded)
    if case == 'move_head':
        preview_due = True
    run_stage(case, 28)
    assert max(abs(v) for v in useful(case, 'forward')) > 25.0, case

head_yaw = command_yaw('move_head')
hand_right_dominant = command_yaw('move_hand_rd')
raw_right_dominant = command_yaw('move_raw_rd')
hand_left_dominant = command_yaw('move_hand_ld')
raw_left_dominant = command_yaw('move_raw_ld')
assert abs(angle_delta(hand_right_dominant, raw_left_dominant)) < 18.0, \
    (hand_right_dominant, raw_left_dominant)
assert abs(angle_delta(raw_right_dominant, hand_left_dominant)) < 18.0, \
    (raw_right_dominant, hand_left_dominant)
assert abs(angle_delta(head_yaw, hand_right_dominant)) > 25.0, \
    (head_yaw, hand_right_dominant)
movement_origin_end = player_origin()
assert distance(movement_origin_start, movement_origin_end) > 8.0, \
    (movement_origin_start, movement_origin_end)
results['mode_command_yaws'] = dict(head=head_yaw, offhand_right_dominant=hand_right_dominant,
    dominant_right=raw_right_dominant, offhand_left_dominant=hand_left_dominant,
    dominant_left=raw_left_dominant, authoritative_origin_delta=distance(movement_origin_start, movement_origin_end))

# Focus loss while the stick stays held must clear movement; restoring focus
# while still held stays neutral-gated until a neutral sample, then rearms.
set_stage_cvars('focus_lost', 2, 1)
run_stage('focus_lost', 18)
run_stage('focus_held', 18)
run_stage('rearm_neutral', 14)
run_stage('focus_rearmed', 22)
blocked = max(abs(v) for name in ('focus_lost', 'focus_held') for v in useful(name, 'forward'))
rearmed = statistics.median(useful('focus_rearmed', 'forward'))
assert blocked < 5.0 and rearmed > 35.0, (blocked, rearmed)
assert max(abs(v) for v in useful('rearm_neutral', 'forward')) < 5.0
results['focus_neutral_rearm'] = dict(blocked_forward=blocked, rearmed_forward=rearmed)

# Native forward and speed keys enter through ordinary +command handling. VR
# movement remains an additive contribution, including the source run mapping.
set_stage_cvars('desktop_base', 2, 1)
cbuf('+forward 119\n')
run_stage('desktop_base', 22)
run_stage('vr_mix', 22)
cbuf('+speed 120\n')
run_stage('vr_speed_mix', 22)
cbuf('-forward 119\n')
run_stage('vr_speed_only', 22)
cbuf('-speed 120\n')
desktop = statistics.median(useful('desktop_base', 'forward'))
mixed = statistics.median(useful('vr_mix', 'forward'))
speed_mixed = statistics.median(useful('vr_speed_mix', 'forward'))
speed_vr = statistics.median(useful('vr_speed_only', 'forward'))
assert 150.0 <= desktop <= 250.0, desktop
assert mixed > desktop + 60.0, (desktop, mixed)
assert speed_mixed > mixed * 1.5, (mixed, speed_mixed)
assert speed_vr > 250.0, speed_vr
results['desktop_vr_speed_mix'] = dict(desktop=desktop, mixed=mixed,
    speed_mixed=speed_mixed, speed_vr_only=speed_vr)

# Turn tests use the right stick only. Arrow bindings are unbound above so the
# a user-configured desktop binding cannot add a separate native turn.
set_stage_cvars('turn_neutral', 2, 0)
set_cvars(vr_snap_turn=30, vr_turn_speed=2, vr_180_snap_turn=1)
run_stage('turn_neutral', 12)
turn_start = player_view_yaw()
run_stage('snap_positive', 20)
snap_positive = path_delta('snap_positive', turn_start)
assert -45.0 < snap_positive < -15.0, snap_positive
turn_start = player_view_yaw()
run_stage('snap_reverse', 20)
snap_reverse = path_delta('snap_reverse', turn_start)
assert 15.0 < snap_reverse < 45.0, snap_reverse
run_stage('turn_neutral', 12)
cbuf('vr_turn180\n')
turn_start = player_view_yaw()
run_stage('turn_180', 16)
turn_180 = path_delta('turn_180', turn_start)
assert abs(abs(angle_delta(turn_180, 0.0)) - 180.0) < 20.0, turn_180
run_stage('turn_neutral', 12)
set_cvars(vr_snap_turn=0, vr_turn_speed=2, vr_joystick_yaw_multi=1)
run_stage('turn_neutral', 12)
turn_start = player_view_yaw()
run_stage('turn_smooth', 38)
smooth_steps = [angle_delta(b, a) for a, b in zip(
    [turn_start] + view_samples.get('turn_smooth', []), view_samples.get('turn_smooth', []))]
smooth_total = sum(smooth_steps)
assert smooth_total < -5.0 and abs(smooth_total) < 120.0, smooth_total
assert all(step <= 0.25 and step > -10.0 for step in smooth_steps), smooth_steps
results['turns'] = dict(snap_positive=snap_positive, snap_reverse=snap_reverse,
    turn_180=turn_180, smooth_total=smooth_total, smooth_steps=len(smooth_steps))

# Exercise real native no-send and catch-up schedules using donor cvars, not
# a replacement clock. Prepared VR movement survives no-send frames and is
# consumed once on send; same-frame catch-up retains only its angle basis.
original_maxfps = cvar('host_maxfps')
set_stage_cvars('timing', 0, 0)
set_cvars(host_phys_max_ticrate=5)
run_stage('timing', 40)
sent_hosts = {sample['host'] for sample in command_samples['timing']}
unsent = [sample for sample in timing_records if sample['host'] not in sent_hosts and sample['pending']]
consumed = [sample for sample in timing_records if sample['host'] in sent_hosts]
assert unsent and consumed and results.get('no_send_preview'), (unsent, consumed)
assert all(abs(sample['forward']-200) < .01 for sample in unsent), unsent
assert all(not sample['pending'] and sample['angles'] for sample in consumed), consumed
stage = 'neutral'
set_cvars(host_phys_max_ticrate=72, host_maxfps=20)
run_stage('neutral', 12)
run_stage('timing_catchup', 12)
groups = {}
for sample in command_samples['timing_catchup']:
    groups.setdefault(sample['host'], []).append(sample)
catchups = [group for group in groups.values() if len(group) > 1]
assert catchups, groups
for group in catchups:
    assert abs(group[0]['forward']-200) < .01, group
    assert all(abs(sample['forward']) < .01 for sample in group[1:]), group
    assert all(sample['angles'] == group[0]['angles'] for sample in group), group
results['native_pending_lifecycle'] = dict(no_send_frames=len(unsent),
    consumed_frames=len(consumed), catchup_frames=len(catchups),
    repeated_angles_preserved=True, vr_velocity_consumed_once=True)
stage = 'neutral'
set_cvars(host_phys_max_ticrate=0, host_maxfps=original_maxfps)

# Save evidence before assertions at process shutdown; the basedir is disposable.
results['server_origin_final'] = player_origin()
results['server_displacement'] = distance(movement_origin_start, results['server_origin_final'])
assert input_counts.get('focus_lost', 0) >= 18 and pose_counts.get('turn_smooth', 0) >= 30
assert preview_done, 'the active pending VR movement was not previewed'
with open(os.environ['QSVR_LOCOMOTION_RESULT'], 'w') as output:
    json.dump(results, output, indent=2)
print('QSVR_VR_LOCOMOTION_PASSED ' + json.dumps(results, separators=(',', ':')))
end
quit
