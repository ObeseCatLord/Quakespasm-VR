# Native SDL3 virtual-gamepad proof for desktop controller defaults and wheel.
#
# Run only on an isolated Xvfb DISPLAY and basedir/profile:
# QSVR_GAMEPAD_PRIVATE_DISPLAY=1 SDL_JOYSTICK_HIDAPI=0 \
# QSVR_GAMEPAD_BASEDIR=/tmp/qsvr-gamepad-lab/assets \
# QSVR_GAMEPAD_USERDIR=/tmp/qsvr-gamepad-lab/user \
# QSVR_GAMEPAD_RESULT=/tmp/qsvr-gamepad-result.json \
# gdb -q --batch -x tests/desktop_gamepad_input.gdb /path/to/vkquake
#
# Optional QSVR_GAMEPAD_MOD and QSVR_GAMEPAD_MAP choose an installed private
# game directory/map. The test never seeds inventory or fabricates wheel hover.
set pagination off
set confirm off
set debuginfod enabled off
set print thread-events off
python
import ctypes as C, gdb, json, os, time

samples = []
phase = 'startup'
result_path = os.environ.get('QSVR_GAMEPAD_RESULT', '/tmp/desktop-gamepad-result.json')
# Pinned SDL3 enum values and interface offsets from SDL_joystick.h. The
# distribution build intentionally omits third-party SDL type DWARF.
AX_LEFTX, AX_LEFTY, AX_RIGHTX, AX_RIGHTY, AX_LEFT_TRIGGER, AX_RIGHT_TRIGGER = range(6)
BTN_SOUTH, BTN_EAST, BTN_WEST, BTN_NORTH, BTN_BACK = range(5)
BTN_RIGHT_STICK, BTN_LEFT_SHOULDER, BTN_RIGHT_SHOULDER = 8, 9, 10
SENSOR_GYRO = 3
# SDL compacts virtual joystick button indices from the descriptor's mask.
VIRTUAL_BUTTON = {BTN_SOUTH: 0, BTN_EAST: 1, BTN_WEST: 2, BTN_NORTH: 3,
                  BTN_BACK: 4, BTN_RIGHT_STICK: 5, BTN_LEFT_SHOULDER: 6,
                  BTN_RIGHT_SHOULDER: 7}

def run(command): return gdb.execute(command, to_string=True)
def iv(expr): return int(gdb.parse_and_eval(expr))
def fv(expr): return float(gdb.parse_and_eval(expr))
def text(expr): return gdb.parse_and_eval(expr).string()
def call(expr): run('call (void)' + expr)
def require(condition, message):
    if not condition: raise AssertionError('%s: %s' % (phase, message))
def commands(value):
    call('Cbuf_AddText(' + json.dumps(value + '\n') + ')')
    call('Cbuf_Execute()')
def frame(): call('SCR_UpdateScreen(0)')
def snap(label):
    global phase
    phase = label
    sample = dict(case=label,
                  wheel_open=iv('VR_WeaponMenu_IsOpen()'),
                  wheel_held=iv('in_vr_weaponmenu.state & 1'),
                  gamepad=iv('joy_active_controller != 0'),
                  gyro_enable=fv('Cvar_VariableValue("gyro_enable")'),
                  yaw=fv('cl.viewangles[1]'),
                  pitch=fv('cl.viewangles[0]'),
                  impulse=iv('in_impulse'))
    samples.append(sample)
    return sample
def pump():
    call('SDL_UpdateJoysticks()')
    call('IN_SendKeyEvents()')
    call('IN_Commands()')
    call('Cbuf_Execute()')
def axis(axis_id, value):
    require(iv('(int)SDL_SetJoystickVirtualAxis($vjoy, %s, %s)' % (axis_id, value)),
            'SDL rejected virtual axis update')
def button(button_id, down):
    require(button_id in VIRTUAL_BUTTON, 'unsupported virtual gamepad button')
    require(iv('(int)SDL_SetJoystickVirtualButton($vjoy, %s, %d)' %
               (VIRTUAL_BUTTON[button_id], down)),
            'SDL rejected virtual button update')
def neutral_axes():
    for axis_id in (AX_LEFTX, AX_LEFTY, AX_RIGHTX, AX_RIGHTY):
        axis(axis_id, 0)
    # SDL3 virtual trigger axes rest at their documented minimum value.
    axis(AX_LEFT_TRIGGER, -32768)
    axis(AX_RIGHT_TRIGGER, -32768)
def move_once():
    call('memset($cmd,0,sizeof(usercmd_t))')
    call('IN_Move($cmd)')
def mouse_motion(x, y):
    # SDL3 does not accept pushed mouse motion here. Move only the inferior's
    # X11 window through XTest so SDL updates its actual absolute pointer.
    lib = C.CDLL('libX11.so.6')
    lib.XOpenDisplay.restype = C.c_void_p
    lib.XOpenDisplay.argtypes = [C.c_char_p]
    display = lib.XOpenDisplay(os.environ['DISPLAY'].encode())
    require(bool(display), 'private X11 display unavailable')
    lib.XDefaultRootWindow.argtypes = [C.c_void_p]
    lib.XDefaultRootWindow.restype = C.c_ulong
    root = lib.XDefaultRootWindow(display)
    lib.XInternAtom.argtypes = [C.c_void_p, C.c_char_p, C.c_int]
    lib.XInternAtom.restype = C.c_ulong
    atom = lib.XInternAtom(display, b'_NET_WM_PID', 1)
    lib.XQueryTree.argtypes = [C.c_void_p, C.c_ulong, C.POINTER(C.c_ulong),
                              C.POINTER(C.c_ulong), C.POINTER(C.POINTER(C.c_ulong)),
                              C.POINTER(C.c_uint)]
    lib.XGetWindowProperty.argtypes = [C.c_void_p, C.c_ulong, C.c_ulong,
                                      C.c_long, C.c_long, C.c_int, C.c_ulong,
                                      C.POINTER(C.c_ulong), C.POINTER(C.c_int),
                                      C.POINTER(C.c_ulong), C.POINTER(C.c_ulong),
                                      C.POINTER(C.c_void_p)]
    lib.XFree.argtypes = [C.c_void_p]
    found, queue = None, [(root, 0)]
    while queue and found is None:
        parent, depth = queue.pop()
        children = C.POINTER(C.c_ulong)()
        r, p, count = C.c_ulong(), C.c_ulong(), C.c_uint()
        if not lib.XQueryTree(display, parent, C.byref(r), C.byref(p), C.byref(children), C.byref(count)):
            continue
        for n in range(min(count.value, 256)):
            window = children[n]
            actual, fmt, items, remaining, data = C.c_ulong(), C.c_int(), C.c_ulong(), C.c_ulong(), C.c_void_p()
            if atom:
                lib.XGetWindowProperty(display, window, atom, 0, 1, 0, 6,
                                       C.byref(actual), C.byref(fmt), C.byref(items),
                                       C.byref(remaining), C.byref(data))
                if data.value:
                    if fmt.value == 32 and items.value and C.cast(data, C.POINTER(C.c_ulong))[0] == gdb.selected_inferior().pid:
                        found = window
                    lib.XFree(data)
            if depth < 2: queue.append((window, depth + 1))
        lib.XFree(children)
    require(found is not None, 'no X11 window belongs to the inferior PID')
    ox, oy, child = C.c_int(), C.c_int(), C.c_ulong()
    lib.XTranslateCoordinates.argtypes = [C.c_void_p, C.c_ulong, C.c_ulong,
        C.c_int, C.c_int, C.POINTER(C.c_int), C.POINTER(C.c_int), C.POINTER(C.c_ulong)]
    require(lib.XTranslateCoordinates(display, found, root, 0, 0,
        C.byref(ox), C.byref(oy), C.byref(child)), 'window/root coordinate translation failed')
    xtest = C.CDLL('libXtst.so.6')
    xtest.XTestFakeMotionEvent.argtypes = [C.c_void_p, C.c_int, C.c_int, C.c_int, C.c_ulong]
    require(xtest.XTestFakeMotionEvent(display, -1, ox.value + x, oy.value + y, 0),
        'private-display XTest motion failed')
    lib.XSync.argtypes = [C.c_void_p, C.c_int]
    lib.XSync(display, 0)
    lib.XCloseDisplay.argtypes = [C.c_void_p]
    lib.XCloseDisplay(display)
    call('IN_SendKeyEvents()')
def open_wheel():
    require(not iv('VR_WeaponMenu_IsOpen()'), 'wheel unexpectedly open before native RTHUMB down')
    button(BTN_RIGHT_STICK, True)
    pump()
    require(iv('VR_WeaponMenu_IsOpen()') and iv('in_vr_weaponmenu.state & 1'),
            'virtual RTHUMB did not open wheel (SDL=%d sampled=%d emitted=%d keydown=%d dest=%d)' %
            (iv('(int)SDL_GetGamepadButton(joy_active_controller, 8)'),
             iv('joy_buttonstate.buttondown[8]'), iv('joy_emittedkeys[8]'),
             iv('keydown[K_RTHUMB]'), iv('key_dest')))
    frame()
def expected_stick_entry(x, y):
    magnitude = (x * x + y * y) ** 0.5
    require(magnitude > 0.0, 'zero stick has no directional native entry')
    center_x = fv('glwidth') * 0.5
    center_y = fv('glheight') * 0.5
    best_dot = -float('inf')
    selected = -1
    for index in range(iv('vr_weapon_menu_frame.count')):
        if not iv('vr_weapon_menu_frame.visible[%d].selectable' % index):
            continue
        dx = fv('vr_weapon_menu_frame.visible[%d].center_x' % index) - center_x
        dy = fv('vr_weapon_menu_frame.visible[%d].center_y' % index) - center_y
        distance = (dx * dx + dy * dy) ** 0.5
        dot = (x * dx + y * dy) / (magnitude * distance) if distance else -float('inf')
        if dot > best_dot:
            best_dot, selected = dot, index
    require(selected >= 0, 'prepared wheel has no selectable native entry')
    return (iv('vr_weapon_menu_frame.visible[%d].entry->id' % selected),
            iv('vr_weapon_menu_frame.visible[%d].entry->impulse' % selected))
def inactive_stick_target():
    center_x = fv('glwidth') * 0.5
    center_y = fv('glheight') * 0.5
    for index in range(iv('vr_weapon_menu_frame.count')):
        visible = 'vr_weapon_menu_frame.visible[%d]' % index
        if iv(visible + '.selectable') and not iv(visible + '.active'):
            dx, dy = fv(visible + '.center_x') - center_x, fv(visible + '.center_y') - center_y
            magnitude = (dx * dx + dy * dy) ** 0.5
            require(magnitude > 0.0, 'inactive native entry is at wheel center')
            return (iv(visible + '.entry->id'), iv(visible + '.entry->impulse'),
                    round(24576 * dx / magnitude), round(24576 * dy / magnitude))
    raise AssertionError('%s: map has no owned inactive native wheel entry' % phase)
def find_selected_entry():
    selected = iv('vr_weapon_menu_hover_id')
    require(selected != -1, 'native stick did not select a prepared wheel entry')
    for index in range(iv('vr_weapon_menu_frame.count')):
        if iv('vr_weapon_menu_frame.visible[%d].entry->id' % index) == selected:
            return index
    raise AssertionError('%s: selected entry is absent from prepared wheel frame' % phase)

virtual_id = 0
try:
    require(os.environ.get('QSVR_GAMEPAD_PRIVATE_DISPLAY') == '1',
            'explicit private-display acknowledgement required')
    require(os.environ.get('SDL_JOYSTICK_HIDAPI') == '0',
            'physical SDL HID drivers must be disabled for this test')
    basedir = os.environ['QSVR_GAMEPAD_BASEDIR']
    userdir = os.environ['QSVR_GAMEPAD_USERDIR']
    require(os.path.isfile(os.path.join(basedir, 'id1', 'pak0.pak')),
            'isolated stock assets missing')
    require(os.path.isdir(os.path.join(userdir, 'id1')),
            'fresh isolated user profile missing')

    started = time.monotonic()
    class Ready(gdb.Breakpoint):
        def stop(self): return iv('cls.signon') == 4 or time.monotonic() - started > 60
    ready = Ready('Host_Frame', internal=True)
    gdb.Breakpoint('Host_Error', internal=True)
    gdb.Breakpoint('Sys_Error', internal=True)
    mod = os.environ.get('QSVR_GAMEPAD_MOD', '')
    mapname = os.environ.get('QSVR_GAMEPAD_MAP', 'e1m1')
    require(not mod or (mod not in ('.', '..') and all(c.isalnum() or c in '._-' for c in mod)),
            'mod must be one game-directory name')
    require(mapname and all(c.isalnum() or c in '._-/' for c in mapname), 'invalid map name')
    launch = ('run -basedir ' + json.dumps(basedir) + ' -userdir ' + json.dumps(userdir) +
              ' -novr -nosound -window -width 640 -height 480')
    if mod: launch += ' -game ' + mod
    run(launch + ' +map ' + mapname)
    require(gdb.newest_frame().name() == 'Host_Frame' and iv('cls.signon') == 4,
            'startup did not sign on')
    ready.delete()
    # Let the real local client clear spawn's setangle lock before checking
    # physical stick look; no input is injected during this warm-up.
    class Warmup(gdb.Breakpoint):
        def stop(self): return True
    warmup = Warmup('Host_Frame', internal=True)
    for unused in range(64):
        if not iv('CL_AngleLocked()') and iv('cl.movemessages') >= 3:
            break
        run('continue')
    warmup.delete()
    require(not iv('CL_AngleLocked()') and iv('cl.movemessages') >= 3,
            'native spawn lock did not expire before controller proof')
    samples.append(dict(case='launch_context', mod=mod or 'id1', map=mapname))
    require(not iv('vulkan_globals.stereo_active'), 'desktop test attached XR')
    require(not iv('joy_active_controller'), 'physical or pre-existing gamepad is active')

    run('set $cmd = (usercmd_t *)calloc(1,sizeof(usercmd_t))')
    run('set $desc = (unsigned char *)calloc(1,136)')
    run('set $sensor = (unsigned char *)calloc(1,8)')
    run('set $gyro = (float *)calloc(3,sizeof(float))')
    run('set $vjoy_name = (char *)strdup("QSVR private SDL3 virtual gamepad")')
    run('set *(unsigned int *)($desc + 0) = 136')
    run('set *(unsigned short *)($desc + 4) = 1')
    run('set *(unsigned short *)($desc + 12) = 6')
    run('set *(unsigned short *)($desc + 14) = 8')
    run('set *(unsigned short *)($desc + 22) = 1')
    run('set *(unsigned int *)($desc + 28) = 0x71f')
    run('set *(unsigned int *)($desc + 32) = 0x3f')
    run('set *(void **)($desc + 40) = $vjoy_name')
    run('set *(void **)($desc + 56) = $sensor')
    run('set *(int *)($sensor + 0) = 3')
    run('set *(float *)($sensor + 4) = 60.0f')
    run('set $virtual_id = (int)SDL_AttachVirtualJoystick((void *)$desc)')
    virtual_id = iv('$virtual_id')
    require(virtual_id != 0, 'SDL_AttachVirtualJoystick failed')
    call('IN_SendKeyEvents()')
    require(iv('joy_active_controller != 0'), 'SDL gamepad-added event did not reach native owner')
    run('set $vjoy = (void *)SDL_GetGamepadJoystick(joy_active_controller)')
    require(iv('$vjoy != 0') and iv('(int)SDL_IsJoystickVirtual($virtual_id)'),
            'native owner did not open the attached virtual gamepad')
    require(iv('(int)SDL_GamepadHasSensor(joy_active_controller, 3)'),
            'attached virtual gyro is unavailable')
    require(iv('(int)SDL_SetGamepadSensorEnabled(joy_active_controller, 3, 1)'),
            'attached virtual gyro cannot be enabled')
    neutral_axes()
    pump()
    frame()

    require(fv('Cvar_VariableValue("gyro_enable")') == 0.0,
            'fresh desktop gyro default is not opt-in')
    commands('joy_defaultbindings')
    require(text('keybindings[K_LSHOULDER]') == 'impulse 12' and
            text('keybindings[K_RSHOULDER]') == 'impulse 10' and
            text('keybindings[K_LTRIGGER]') == '+jump' and
            text('keybindings[K_RTRIGGER]') == '+attack' and
            text('keybindings[K_RTHUMB]') == '+vr_weaponmenu',
            'native desktop default controller bindings differ from the expected baseline')
    require(not iv('keybindings[K_VR_RTHUMB]'),
            'desktop default command populated dedicated XR RTHUMB')
    snap('native_defaults_loaded')

    # A real SDL sensor update must not rotate the camera while gyro is off.
    run('set $gyro[0] = 0.0f')
    run('set $gyro[1] = 8.0f')
    run('set $gyro[2] = 0.0f')
    yaw_before_gyro = fv('cl.viewangles[1]')
    require(iv('(int)SDL_SendJoystickVirtualSensorData($vjoy, 3, 1, $gyro, 3)'),
            'SDL rejected virtual gyro sample')
    pump()
    move_once()
    require(fv('cl.viewangles[1]') == yaw_before_gyro,
            'disabled gyro changed the camera')
    snap('gyro_sensor_ignored_by_default')

    # Normal right-stick look works before the wheel opens.
    axis(AX_RIGHTX, 24576)
    pump()
    yaw_before_look = fv('cl.viewangles[1]')
    move_once()
    require(fv('cl.viewangles[1]') != yaw_before_look,
            'normal virtual right-stick look did not turn the camera')
    neutral_axes()
    pump()

    # Right-thumb opens the real wheel. The axis is sampled before its release,
    # chooses a rendered entry through the native stick owner, and must not turn.
    open_wheel()
    target_id, expected_impulse, target_x, target_y = inactive_stick_target()
    axis(AX_RIGHTX, target_x)
    axis(AX_RIGHTY, target_y)
    pump()
    frame()
    selected_index = find_selected_entry()
    expected_id = iv('vr_weapon_menu_frame.visible[%d].entry->id' % selected_index)
    require(expected_id == target_id, 'native stick did not choose the requested inactive entry')
    yaw_before_wheel = fv('cl.viewangles[1]')
    move_once()
    require(fv('cl.viewangles[1]') == yaw_before_wheel,
            'open wheel allowed virtual look-stick camera motion')
    snap('stick_selected_native_entry_camera_suppressed')

    button(BTN_RIGHT_STICK, False)
    pump()
    require(not iv('VR_WeaponMenu_IsOpen()') and iv('in_impulse') == expected_impulse,
            'final RTHUMB release missed impulse (open=%d SDL=%d sampled=%d emitted=%d keydown=%d got=%d expected=%d)' %
            (iv('VR_WeaponMenu_IsOpen()'), iv('(int)SDL_GetGamepadButton(joy_active_controller, 8)'),
             iv('joy_buttonstate.buttondown[8]'), iv('joy_emittedkeys[8]'), iv('keydown[K_RTHUMB]'),
             iv('in_impulse'), expected_impulse))
    yaw_after_release, pitch_after_release = fv('cl.viewangles[1]'), fv('cl.viewangles[0]')
    move_once()
    require(fv('cl.viewangles[1]') == yaw_after_release and fv('cl.viewangles[0]') == pitch_after_release,
            'same-poll RTHUMB release allowed look before the next neutral axis poll')
    neutral_axes()
    pump()
    require(iv('(int)SDL_GetGamepadAxis(joy_active_controller, 2)') == 0 and
            iv('(int)SDL_GetGamepadAxis(joy_active_controller, 3)') == 0,
            'final virtual stick axes did not return neutral')
    snap('final_release_and_axes_neutral')

    class NextFrame(gdb.Breakpoint):
        def stop(self): return True
    next_frame = NextFrame('Host_Frame', internal=True)
    selected = 'vr_weapon_menu_frame.visible[%d].entry' % selected_index
    equipped = False
    for unused in range(128):
        run('continue')
        if iv('VR_WeaponMenu_EntryActive(%s,cl.stats,sizeof(cl.stats)/sizeof(cl.stats[0]))' % selected):
            equipped = True
            break
    next_frame.delete()
    require(equipped, 'real QuakeC did not equip the stick-selected weapon')
    samples.append(dict(case='native_qc_equip', entry_id=expected_id,
                        impulse=expected_impulse, client_weapon=iv('cl.stats[10]')))

    # BACK remains the physical TAB key: native down shows scores and the
    # matching release clears it without relying on a synthetic binding state.
    commands('bind TAB +showscores')
    button(BTN_BACK, True)
    pump()
    require(iv('sb_showscores'), 'virtual BACK did not press the TAB scoreboard binding')
    button(BTN_BACK, False)
    pump()
    require(not iv('sb_showscores'), 'virtual BACK release left the scoreboard held')
    snap('back_tab_scoreboard_release')

    # Stationary pointer polling must not reclaim selection after one genuine
    # above-deadzone look-stick deflection.
    neutral_axes()
    pump()
    open_wheel()
    for unused in range(3):
        frame()
    axis(AX_RIGHTX, 24576)
    pump()
    frame()
    stick_id, unused_impulse = expected_stick_entry(1.0, 0.0)
    require(iv('vr_weapon_menu_desktop_owner') == 1 and iv('vr_weapon_menu_hover_id') == stick_id,
            'one above-deadzone stick deflection did not acquire native wheel selection')
    for unused in range(3):
        frame()
    require(iv('vr_weapon_menu_desktop_owner') == 1 and iv('vr_weapon_menu_hover_id') == stick_id,
            'idle mouse polling reclaimed stick-owned native wheel selection')
    button(BTN_RIGHT_STICK, False)
    pump()
    neutral_axes()
    pump()
    snap('idle_mouse_polls_do_not_reclaim_stick')

    # Actual private-X11 pointer movement owns the wheel; a changed axis inside
    # its selected deadzone must not take it back.
    open_wheel()
    mouse_motion(320, 240)
    frame()
    require(iv('vr_weapon_menu_desktop_owner') == 0, 'native mouse event did not acquire wheel ownership')
    axis(AX_RIGHTX, 1000)
    pump()
    require(iv('vr_weapon_menu_desktop_owner') == 0,
            'sub-deadzone stick change took native mouse-owned wheel selection')
    # The mouse serial is consumed by each idle controller poll. One later
    # deliberate deflection must take ownership, not remain stale-mouse gated.
    for unused in range(3):
        pump()
    axis(AX_RIGHTX, 24576)
    pump()
    frame()
    post_idle_stick_id, unused_impulse = expected_stick_entry(1.0, 0.0)
    require(iv('vr_weapon_menu_desktop_owner') == 1 and iv('vr_weapon_menu_hover_id') == post_idle_stick_id,
            'one post-idle above-deadzone stick deflection did not take mouse-owned wheel')
    button(BTN_RIGHT_STICK, False)
    pump()
    neutral_axes()
    pump()
    snap('subdeadzone_stick_does_not_take_mouse')

    # IN_Commands samples logical stick axes before releasing RTHUMB in the
    # same SDL poll, so this release executes the direction selected that poll.
    open_wheel()
    same_poll_id, same_poll_impulse, same_poll_x, same_poll_y = inactive_stick_target()
    axis(AX_RIGHTX, same_poll_x)
    axis(AX_RIGHTY, same_poll_y)
    button(BTN_RIGHT_STICK, False)
    pump()
    require(not iv('VR_WeaponMenu_IsOpen()') and iv('in_impulse') == same_poll_impulse,
            'same-poll right-stick deflection and RTHUMB release lost native selection')
    neutral_axes()
    pump()
    samples.append(dict(case='same_poll_axis_release', entry_id=same_poll_id,
                        impulse=same_poll_impulse))

    # Selector routing follows the configured logical axes for both Move and
    # swapped Look; neither case reaches into dedicated XR controller keys.
    commands('joy_wheel_axis 1')
    open_wheel()
    move_id, unused_impulse = expected_stick_entry(1.0, 0.0)
    axis(AX_LEFTX, 24576)
    pump()
    frame()
    require(iv('vr_weapon_menu_hover_id') == move_id,
            'Move wheel-axis setting ignored the physical left stick')
    button(BTN_RIGHT_STICK, False)
    pump()
    neutral_axes()
    pump()
    commands('joy_wheel_axis 0; joy_swapmovelook 1')
    open_wheel()
    swapped_id, unused_impulse = expected_stick_entry(1.0, 0.0)
    axis(AX_LEFTX, 24576)
    pump()
    frame()
    require(iv('vr_weapon_menu_hover_id') == swapped_id,
            'swapped Look selector ignored the physical left stick')
    button(BTN_RIGHT_STICK, False)
    pump()
    neutral_axes()
    pump()
    commands('joy_swapmovelook 0; joy_wheel_axis 0')
    snap('logical_move_and_swapped_look_selector')

    # A keyboard hold and native RTHUMB share the wheel. Unplug may release
    # only the controller token; keyboard Q must close the remaining hold.
    commands('bind q +vr_weaponmenu')
    call('Key_Event(113,1)')
    call('Cbuf_Execute()')
    require(iv('VR_WeaponMenu_IsOpen()') and iv('keydown[113]'),
            'keyboard Q did not open the shared wheel hold')
    button(BTN_RIGHT_STICK, True)
    pump()
    require(iv('VR_WeaponMenu_IsOpen()') and iv('in_vr_weaponmenu.state & 1'),
            'native RTHUMB did not join keyboard wheel hold')
    button(BTN_BACK, True)
    pump()
    call('Key_Event(K_TAB,1)')
    call('Cbuf_Execute()')
    require(iv('sb_showscores'), 'physical TAB did not join native BACK scoreboard hold')
    require(iv('(int)SDL_DetachVirtualJoystick($virtual_id)'), 'SDL virtual gamepad detach failed')
    virtual_id = 0
    call('IN_SendKeyEvents()')
    require(not iv('joy_active_controller') and iv('keydown[113]') and iv('VR_WeaponMenu_IsOpen()') and iv('sb_showscores'),
            'native unplug erased keyboard Q wheel token or physical TAB scoreboard hold')
    call('Key_Event(K_TAB,0)')
    call('Cbuf_Execute()')
    require(not iv('sb_showscores'), 'physical TAB release did not close scoreboard after native BACK unplug')
    call('Key_Event(113,0)')
    call('Cbuf_Execute()')
    require(not iv('VR_WeaponMenu_IsOpen()') and not iv('in_vr_weaponmenu.state & 1'),
            'keyboard Q release did not close wheel after native RTHUMB unplug')
    snap('unplug_retains_keyboard_q_until_release')

    result = dict(ok=True, samples=samples)
except Exception as error:
    result = dict(ok=False, phase=phase, error=repr(error), samples=samples)
finally:
    if virtual_id:
        try: call('SDL_DetachVirtualJoystick($virtual_id)')
        except gdb.error: pass
    with open(result_path, 'w') as output: json.dump(result, output, indent=2)
    print('DESKTOP_GAMEPAD_RESULT ' + json.dumps(result), flush=True)
    try: run('kill') # Prevent config writes even in the isolated profile.
    except gdb.error: pass
if not result['ok']: gdb.execute('quit 1')
end
quit 0
