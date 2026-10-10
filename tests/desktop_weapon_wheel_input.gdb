# Linux SDL2/SDL3 native desktop lifecycle qualification, AFTER integration.
# Requires an isolated X11 DISPLAY (e.g. existing private Xvfb harness), debug
# binary, packaged default.cfg and id1 assets in an isolated basedir.
# Optional QSVR_WHEEL_MOD=peril3.0 QSVR_WHEEL_MAP=start uses connected Peril.
# This fixture selects current owned inventory; it does not seed eight weapons.
# Only the private Xvfb virtual pointer is moved; no physical input or foreground focus.
# QSVR_WHEEL_PRIVATE_DISPLAY=1 QSVR_WHEEL_BASEDIR=/tmp/wheel-lab \
# QSVR_WHEEL_RESULT=/tmp/wheel-result.json \
# gdb -q --batch -x tests/desktop_weapon_wheel_input.gdb /path/to/debug/vkquake
# Run from the repository root. Use timeout 120s around GDB as an outer bound.
# Private XTest motion must update SDL's actual absolute pointer; failure
# is reported, never replaced with manufactured renderer hover state.
set pagination off
set confirm off
set debuginfod enabled off
set print thread-events off
python
import ctypes as C, gdb, json, os, time

samples = []
phase = 'startup'
result_path = os.environ.get('QSVR_WHEEL_RESULT', '/tmp/desktop-wheel-result.json')
def run(command): return gdb.execute(command, to_string=True)
def iv(expr): return int(gdb.parse_and_eval(expr))
def fv(expr): return float(gdb.parse_and_eval(expr))
def text(expr): return gdb.parse_and_eval(expr).string()
def call(expr): run('call (void)' + expr)
def commands(value):
    call('Cbuf_AddText(' + json.dumps(value + '\n') + ')')
    call('Cbuf_Execute()')
def key(code, down, flush=True):
    call('Key_Event(%s,%d)' % (code, down))
    if flush: call('Cbuf_Execute()')
def relative():
    return iv('(int)SDL_GetWindowRelativeMouseMode((SDL_Window *)VID_GetWindow())'
              if sdl3 else '(int)SDL_GetRelativeMouseMode()')
def snap(label):
    global phase
    phase = label
    sample = dict(case=label, open=iv('VR_WeaponMenu_IsOpen()'),
                  held=iv('in_vr_weaponmenu.state & 1'), relative=relative(),
                  attack=iv('in_attack.state & 3'), forward=iv('in_forward.state & 1'),
                  dx=fv('total_dx'), dy=fv('total_dy'),
                  angle_locked=iv('CL_AngleLocked()'), fov=fv('r_refdef.basefov'),
                  yaw=fv('cl.viewangles[1]'), pitch=fv('cl.viewangles[0]'),
                  impulse=iv('in_impulse'))
    samples.append(sample)
    return sample

def require(condition, message):
    if not condition: raise AssertionError('%s: %s' % (phase, message))
def closed(label, captured=1):
    s = snap(label)
    require(not s['open'] and not s['held'] and s['relative'] == captured,
            'wheel/capture did not close cleanly')
    require(s['dx'] == s['dy'] == 0, 'stale mouse motion survived')
def open_q():
    key('113', True)
    s = snap('open_q')
    require(s['open'] and s['held'] and not s['relative'], 'Q did not open absolute wheel')
def event(kind, fields, pump=True):
    call('memset($event,0,sizeof(SDL_Event))')
    run('set $event->type = ' + kind)
    for name, value in fields.items(): run('set $event->%s = %s' % (name, value))
    require(iv('(int)SDL_PushEvent($event)') == 1, 'SDL rejected synthetic event')
    if pump: call('IN_SendKeyEvents()')
def frame(): call('SCR_UpdateScreen(0)')
def capture(label):
    commands('screenshot')
    filename = text('screenshot_imagename')
    frame()
    call('GL_SynchronizeEndRenderingTask()')
    samples.append(dict(case=label, screenshot=filename))

# Private X11 motion to only the inferior's _NET_WM_PID window. SDL_PushEvent
# motion alone does not update SDL_GetMouseState, so it cannot prove release.
def x11_pointer(x, y):
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
    found = None
    queue = [(root, 0)]
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
    # This fixture requires a PRIVATE Xvfb display. XSendEvent motion does not
    # update SDL3's absolute mouse state here; move only its virtual XTest pointer.
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

try:
    require(os.environ.get('QSVR_WHEEL_PRIVATE_DISPLAY') == '1', 'explicit private-display acknowledgement required')
    basedir = os.environ['QSVR_WHEEL_BASEDIR']
    require(os.path.isfile(os.path.join(basedir, 'id1', 'pak0.pak')), 'isolated stock assets missing')
    started = time.monotonic()
    class Ready(gdb.Breakpoint):
        def stop(self): return iv('cls.signon') == 4 or time.monotonic() - started > 60
    ready = Ready('Host_Frame', internal=True)
    gdb.Breakpoint('Host_Error', internal=True)
    gdb.Breakpoint('Sys_Error', internal=True)
    mod = os.environ.get('QSVR_WHEEL_MOD', '')
    mapname = os.environ.get('QSVR_WHEEL_MAP', 'start' if mod == 'peril3.0' else 'e1m1')
    require(not mod or (mod not in ('.','..') and all(c.isalnum() or c in '._-' for c in mod)),
            'mod must be a single game-directory name')
    require(mapname and all(c.isalnum() or c in '._-/' for c in mapname), 'invalid map name')
    launch = 'run -basedir ' + json.dumps(basedir) + ' -novr -nosound -window -width 640 -height 480'
    if mod: launch += ' -game ' + mod
    run(launch + ' +map ' + mapname)
    samples.append(dict(case='launch_context', mod=mod or 'id1', map=mapname, inventory_seeded=False))
    require(gdb.newest_frame().name() == 'Host_Frame' and iv('cls.signon') == 4, 'startup did not sign on')
    ready.delete()
    # Let the real local client receive subsequent snapshots after spawn's
    # setangle. Mouse motion is deliberately ignored during that native lock.
    class Warmup(gdb.Breakpoint):
        def stop(self): return True
    warmup = Warmup('Host_Frame', internal=True)
    for n in range(64):
        if not iv('CL_AngleLocked()') and fv('r_refdef.basefov') > 0 and iv('cl.movemessages') >= 3: break
        run('continue')
    warmup.delete()
    require(not iv('CL_AngleLocked()') and iv('cl.movemessages') >= 3, 'native spawn lock/initial discarded move commands did not expire naturally')
    try:
        iv('SDL_EVENT_MOUSE_BUTTON_DOWN')
        sdl3 = True
    except gdb.error: sdl3 = False
    require(not iv('vulkan_globals.stereo_active'), 'desktop test attached XR')
    run('set $event = (SDL_Event *)calloc(1,sizeof(SDL_Event))')
    run('set $cmd = (usercmd_t *)calloc(1,sizeof(usercmd_t))')
    run('set $mousepos = (int *)calloc(2,sizeof(int))')
    call('IN_WindowFocusChanged(1)') # Native focus seam on the private display only.
    frame() # Initialize native view/FOV before checking resumed relative aim.
    if os.environ.get('QSVR_WHEEL_GIVE_ALL') == '1':
        require(mod == 'peril3.0', 'inventory grant is scoped to the Peril roster case')
        commands('sv_giveall all') # Existing native admin command, including its QC adapter.
        grant = Warmup('Host_Frame', internal=True)
        for n in range(64):
            run('continue')
            if iv('cl.items & 127') == 127: break
        grant.delete()
        require(iv('cl.items & 127') == 127, 'native give-all did not reach client inventory')
        samples.append(dict(case='native_admin_inventory_grant', command='sv_giveall all', items=iv('cl.items')))
    require(iv('keybindings[113]') and text('keybindings[113]') == '+vr_weaponmenu',
            'fresh isolated startup did not load packaged Q default (remove saved configs for this case)')
    commands('exec default.cfg')
    require(text('keybindings[113]') == '+vr_weaponmenu', 'reset packaged default lacks Q')
    commands('bind q +jump')
    call('IN_UpdateInputMode()')
    require(text('keybindings[113]') == '+jump', 'saved remap overwritten')
    commands('unbind q')
    call('IN_UpdateInputMode()')
    require(not iv('keybindings[113]'), 'explicit unbind overwritten')
    commands('exec default.cfg\nunbindall') # Saved-config unbindall wins after defaults.
    call('IN_UpdateInputMode()')
    require(not iv('keybindings[113]'), 'saved unbindall overwritten')
    commands('exec default.cfg')
    require(text('keybindings[113]') == '+vr_weaponmenu', 'reset failed to restore Q')
    commands('bind r +vr_weaponmenu\nbind MOUSE1 +attack\nbind MOUSE2 +vr_weaponmenu')

    # Actual menu parser/filter keeps the usable row even with a duplicate mod label.
    call('M_Menu_Keys_f()')
    call('M_Keys_AddCustomEntry("+vr_weaponmenu", "")')
    call('M_Keys_AddCustomEntry("impulse 99", "Mod weapon")')
    call('M_Keys_Populate()')
    rows = iv('((vec_header_t *)bindnames)[-1].size')
    wheelrows = [i for i in range(rows) if iv('bindnames[%d].command != 0' % i) and text('bindnames[%d].command' % i) == '+vr_weaponmenu']
    require(len(wheelrows) == 1 and text('bindnames[%d].description' % wheelrows[0]) == 'Weapon Wheel', 'mod bindlist shadowed wheel row')
    run('set keys_cursor = %d' % wheelrows[0])
    call('M_Keys_Key(K_ENTER)')
    require(iv('bind_grab'), 'wheel row could not start bind capture')
    call('M_Keys_Key(113)')
    call('Cbuf_Execute()')
    require(text('keybindings[113]') == '+vr_weaponmenu' and not iv('bind_grab'), 'controls binding failed')
    call('M_Menu_Main_f()')
    call('M_ToggleMenu_f()')
    commands('bind r +vr_weaponmenu\nbind MOUSE2 +vr_weaponmenu')

    key('K_MOUSE1', True)
    require(iv('in_attack.state & 3'), 'held mouse attack prerequisite failed')
    call('IN_MouseMotion(300,-200)') # Accumulate BEFORE opening.
    event('SDL_EVENT_KEY_DOWN' if sdl3 else 'SDL_KEYDOWN',
          {'key.scancode' if sdl3 else 'key.keysym.scancode':'SDL_SCANCODE_Q',
           'key.key' if sdl3 else 'key.keysym.sym':113,
           'key.down' if sdl3 else 'key.state':1}, pump=False)
    event('SDL_EVENT_MOUSE_BUTTON_DOWN' if sdl3 else 'SDL_MOUSEBUTTONDOWN',
          {'button.button':1, 'button.down' if sdl3 else 'button.state':1}, pump=False)
    call('IN_SendKeyEvents()') # Q and mouse press are processed in ONE real SDL pump.
    call('Cbuf_Execute()')
    initial = snap('pending_open_discards_preopen_motion_and_attack')
    require(initial['open'] and initial['dx'] == initial['dy'] == initial['attack'] == 0, 'preopen motion/attack edge leaked')
    angles = [fv('cl.viewangles[%d]' % i) for i in range(3)]
    call('IN_MouseMotion(200,-100)')
    call('IN_MouseMove($cmd)')
    require([fv('cl.viewangles[%d]' % i) for i in range(3)] == angles, 'wheel mouse turned camera')
    require(all(fv('$cmd->%smove_accumulator' % name) == 0 for name in ('forward','side','up')), 'wheel mouse moved player')
    call('IN_HideCursor()')
    call('IN_Activate()')
    require(not relative(), 'cursor reacquisition hid wheel pointer')
    for kind, fields in ((('SDL_EVENT_MOUSE_BUTTON_DOWN' if sdl3 else 'SDL_MOUSEBUTTONDOWN'),
                          {'button.button':1, ('button.down' if sdl3 else 'button.state'):1}),
                         (('SDL_EVENT_MOUSE_WHEEL' if sdl3 else 'SDL_MOUSEWHEEL'), {'wheel.y':1})):
        event(kind, fields)
    call('Cbuf_Execute()')
    require(not iv('in_attack.state & 3'), 'wheel click fired attack')
    key('K_MOUSE1', False)
    key('119', True) # W movement remains available.
    call('CL_BaseMove($cmd)')
    require(iv('in_forward.state & 1') and fv('$cmd->forwardmove') > 0, 'wheel blocked keyboard movement')
    key('119', False)
    key('114', True)
    key('113', False)
    require(iv('VR_WeaponMenu_IsOpen()'), 'first of two wheel sources closed wheel')
    key('114', False)
    closed('final_source_restores_capture')
    call('memset($cmd,0,sizeof(usercmd_t))')
    call('IN_MouseMove($cmd)')
    require([fv('cl.viewangles[%d]' % i) for i in range(3)] == angles,
            'wheel motion changed camera after release')
    require(all(fv('$cmd->%smove_accumulator' % name) == 0 for name in ('forward','side','up')),
            'wheel motion changed movement after release')
    call('IN_MouseMotion(5,0)')
    call('IN_MouseMove($cmd)')
    require(fv('cl.viewangles[1]') != angles[1], 'normal desktop mouse did not resume')
    key('K_CTRL', True)
    key('K_MOUSE1', True)
    open_q()
    require(iv('in_attack.state & 1') and (iv('in_attack.down[0]') == iv('K_CTRL') or
            iv('in_attack.down[1]') == iv('K_CTRL')), 'opening wheel lost keyboard attack contributor')
    key('K_CTRL', False)
    require(not iv('in_attack.state & 1'), 'mouse attack contributor survived wheel opening')
    key('113', False)
    key('K_MOUSE1', False)
    closed('keyboard_attack_contributor_preserved_mouse_released')
    open_q()
    key('K_MOUSE2', True)
    key('113', False)
    require(iv('VR_WeaponMenu_IsOpen()'), 'mouse wheel source was swallowed')
    key('K_MOUSE2', False)
    closed('mouse_bound_source_release')

    open_q()
    event('SDL_EVENT_MOUSE_BUTTON_DOWN' if sdl3 else 'SDL_MOUSEBUTTONDOWN',
          {'button.button':1, 'button.down' if sdl3 else 'button.state':1})
    call('Cbuf_Execute()')
    require(not iv('keydown[K_MOUSE1]'), 'suppressed mouse press acquired a key hold')
    call('IN_CancelDesktopWeaponMenu()')
    commands('bind MOUSE1 +jump')
    event('SDL_EVENT_MOUSE_BUTTON_UP' if sdl3 else 'SDL_MOUSEBUTTONUP',
          {'button.button':1, 'button.down' if sdl3 else 'button.state':0})
    require(iv('cmd_text.cursize') == 0, 'suppressed mouseup dispatched a rebound release after cancellation')
    call('Cbuf_Execute()')
    key('113', False)
    closed('suppressed_mouseup_after_cancel')
    commands('bind MOUSE1 +attack')

    # Cancel while Q remains physically held: it must stop suppressing gameplay mouse.
    open_q()
    before = iv('in_impulse')
    call('IN_CancelDesktopWeaponMenu()')
    closed('canceled_q_still_physically_held')
    require(iv('keydown[113]') and not iv('desktop_weaponmenu_pending[113]') and
            not iv('desktop_weaponmenu_token[113]'), 'cancellation did not retire held Q metadata')
    key('K_MOUSE1', True)
    require(iv('in_attack.state & 1'), 'canceled held Q still suppresses gameplay mouse')
    key('K_MOUSE1', False)
    key('113', False)
    require(iv('in_impulse') == before, 'canceled Q release selected a weapon')

    # Pending-only cancellation must work before the capture/latch exists.
    key('113', True, False)
    call('IN_CancelDesktopWeaponMenu()')
    call('Cbuf_Execute()')
    closed('pending_only_cancel_invalidates_queued_down')
    require(iv('keydown[113]'), 'pending-only cancel unexpectedly cleared physical key')
    key('113', False)

    # An already accepted Q must not stick after a fully queued up/down/up tap.
    open_q()
    before, generation = iv('in_impulse'), iv('VR_WeaponMenu_SessionGeneration()')
    key('113', False, False)
    key('113', True, False)
    key('113', False, False)
    call('Cbuf_Execute()')
    closed('accepted_q_then_queued_up_down_up_cancels_without_selection')
    require(iv('in_impulse') == before and iv('VR_WeaponMenu_SessionGeneration()') == generation,
            'rejected Q tap selected or opened another wheel')
    require(not iv('in_vr_weaponmenu.down[0]') and not iv('in_vr_weaponmenu.down[1]'),
            'retired Q source remained in accepted latch')

    # The same rejected tap retires Q only; an independent accepted R stays held.
    open_q()
    key('114', True)
    before, generation = iv('in_impulse'), iv('VR_WeaponMenu_SessionGeneration()')
    key('113', False, False)
    key('113', True, False)
    key('113', False, False)
    call('Cbuf_Execute()')
    s = snap('accepted_q_r_then_queued_q_up_down_up_preserves_r')
    require(s['open'] and s['held'] and not s['relative'] and iv('keydown[114]') and
            not iv('keydown[113]'), 'rejected Q tap canceled accepted R')
    require(sorted([iv('in_vr_weaponmenu.down[0]'), iv('in_vr_weaponmenu.down[1]')]) == [0,114],
            'rejected Q tap did not leave exactly the accepted R source')
    require(iv('in_impulse') == before and iv('VR_WeaponMenu_SessionGeneration()') == generation,
            'rejected Q tap selected or reopened while R was held')
    key('114', False)
    closed('remaining_r_final_release_after_rejected_q_tap')

    # A fresh R queued after retiring Q must survive; only modal/focus/rebind
    # cancellation invalidates unrelated pending presses.
    open_q()
    before = iv('in_impulse')
    key('113', False, False)
    key('113', True, False)
    key('113', False, False)
    key('114', True, False)
    call('Cbuf_Execute()')
    s = snap('retired_q_preserves_fresh_pending_r')
    require(s['open'] and s['held'] and not s['relative'] and
            sorted([iv('in_vr_weaponmenu.down[0]'), iv('in_vr_weaponmenu.down[1]')]) == [0,114],
            'retiring Q invalidated independently queued R')
    require(iv('in_impulse') == before, 'retired Q selected before fresh R')
    key('114', False)
    closed('fresh_pending_r_final_release')

    # Absolute SDL pointer -> prepared native hover -> release -> real QuakeC outcome.
    open_q()
    frame()
    if os.environ.get('QSVR_WHEEL_GIVE_ALL') == '1':
        require(iv('vr_weapon_menu_frame.count') == 8, 'Peril native owned roster is not eight slots')
    choices = [i for i in range(iv('vr_weapon_menu_frame.count'))
               if iv('vr_weapon_menu_frame.visible[%d].selectable' % i) and
               not iv('vr_weapon_menu_frame.visible[%d].active' % i)]
    require(bool(choices), 'map needs an owned inactive selectable weapon; no inventory seed is performed')
    slot = choices[0]
    run('set $chosen = vr_weapon_menu_frame.visible[%d].entry' % slot)
    expected_id, expected_impulse = iv('$chosen->id'), iv('$chosen->impulse')
    # Convert existing renderer pixels to the known 640x480 window.
    x = round(fv('vr_weapon_menu_frame.visible[%d].center_x' % slot) * 640 / iv('glwidth'))
    y = round(fv('vr_weapon_menu_frame.visible[%d].center_y' % slot) * 480 / iv('glheight'))
    x11_pointer(x, y)
    call('IN_GetMousePos($mousepos,$mousepos+1)')
    require(abs(iv('*$mousepos') - x * iv('vid.width') / 640) <= 1 and
            abs(iv('*($mousepos+1)') - y * iv('vid.height') / 480) <= 1,
            'private XTest did not update actual SDL absolute pointer')
    frame()
    require(iv('vr_weapon_menu_hover_id') == expected_id, 'absolute pointer did not hover intended weapon')
    capture('wheel_absolute_hover')
    key('113', False)
    require(iv('in_impulse') == expected_impulse, 'release did not queue selected impulse')
    closed('absolute_release')
    class NextFrame(gdb.Breakpoint):
        def stop(self): return True
    next_frame = NextFrame('Host_Frame', internal=True)
    for n in range(128):
        run('continue')
        if iv('VR_WeaponMenu_EntryActive($chosen,cl.stats,sizeof(cl.stats)/sizeof(cl.stats[0]))'): break
    next_frame.delete()
    samples.append(dict(case='native_selection_outcome', requested_id=expected_id,
        requested_impulse=expected_impulse, client_weapon=iv('cl.stats[10]'),
        server_weapon=fv('svs.clients[0].edict->v.weapon'),
        queued_impulse=iv('in_impulse'), client_time=fv('cl.time')))
    require(iv('VR_WeaponMenu_EntryActive($chosen,cl.stats,sizeof(cl.stats)/sizeof(cl.stats[0]))'), 'real QC did not activate selected weapon')
    capture('selected_weapon_after_qc')

    for label, action in [('console_cancel','Con_ToggleConsole_f()'),
                          ('menu_cancel','M_Menu_Main_f()'),
                          ('modal_clear_cancel','Key_ClearStates()'),
                          ('renderer_cancel','VR_WeaponMenu_Cancel()')]:
        open_q()
        before = iv('in_impulse')
        call(action)
        call('IN_UpdateInputMode()')
        key('113', False)
        require(iv('in_impulse') == before, 'cancellation selected a weapon')
        closed(label, captured=0 if label in ('console_cancel','menu_cancel') else 1)
        if label == 'console_cancel': call('Con_ToggleConsole_f()')
        if label == 'menu_cancel': call('M_ToggleMenu_f()')
    open_q()
    before = iv('in_impulse')
    saved_mode, saved_ui_mouse = iv('modestate'), fv('ui_mouse.value')
    run('set modestate = MS_FULLSCREEN') # Menu capture policy seam, no real window mode change.
    commands('ui_mouse 0')
    call('M_Menu_Main_f()')
    closed('fullscreen_nomouse_menu_cancel_releases_capture', 0)
    run('set modestate = %d' % saved_mode)
    commands('ui_mouse %f' % saved_ui_mouse)
    call('IN_WindowFocusChanged(0)')
    call('Cbuf_Execute()')
    closed('focus_loss_in_menu', 0)
    call('IN_WindowFocusChanged(1)')
    closed('focus_gain_in_menu_stays_uncaptured', 0)
    key('113', False)
    call('M_ToggleMenu_f()')
    open_q()
    before = iv('in_impulse')
    call('IN_WindowFocusChanged(0)')
    call('Cbuf_Execute()')
    closed('focus_loss_cancel', 0)
    require(iv('in_impulse') == before and not iv('keydown[113]'), 'focus loss selected or left key held')
    call('IN_WindowFocusChanged(1)')
    closed('focus_gain_capture')
    key('113', True, False)
    call('IN_WindowFocusChanged(0)')
    call('IN_WindowFocusChanged(1)')
    call('Cbuf_Execute()')
    closed('queued_press_focus_loss_gain_does_not_reopen')
    key('113', True, False)
    old_token = iv('desktop_weaponmenu_token[113]')
    key('113', False, False) # Queue both old down AND up without executing either.
    call('IN_WindowFocusChanged(0)')
    call('IN_WindowFocusChanged(1)')
    key('113', True, False)
    fresh_token = iv('desktop_weaponmenu_token[113]')
    require(fresh_token and fresh_token != old_token and iv('desktop_weaponmenu_pending[113]'),
            'fresh press did not acquire its own pending token')
    before, generation = iv('in_impulse'), iv('VR_WeaponMenu_SessionGeneration()')
    call('Cbuf_Execute()')
    s = snap('old_down_up_loss_gain_fresh_down_before_cbuf')
    require(s['open'] and s['held'] and not s['relative'] and iv('keydown[113]'),
            'old down/up consumed or closed the fresh press')
    require(iv('VR_WeaponMenu_SessionGeneration()') == generation + 1 and
            iv('in_impulse') == before and iv('desktop_weaponmenu_token[113]') == fresh_token and
            not iv('desktop_weaponmenu_pending[113]'), 'stale command reopened/selected or retired fresh token')
    key('113', False)
    closed('fresh_token_final_release')
    require(not iv('desktop_weaponmenu_token[113]'), 'fresh release did not retire token')
    open_q()
    before = iv('in_impulse')
    commands('unbind q')
    closed('held_source_unbind')
    require(iv('in_impulse') == before, 'rebinding a held wheel selected a weapon')
    key('113', False)
    commands('bind q +vr_weaponmenu')
    open_q()
    signon = iv('cls.signon')
    run('set cls.signon = 0') # Input lifecycle seam; not a network/sign-on proof.
    call('IN_UpdateInputMode()')
    closed('incomplete_signon_cancel')
    run('set cls.signon = %d' % signon)
    key('113', False)
    open_q()
    call('CL_Disconnect()')
    call('Key_UpdateForDest()')
    call('IN_UpdateInputMode()')
    closed('disconnect_cancel', 0)
    result = dict(ok=True, samples=samples)
except Exception as error:
    import traceback
    traceback.print_exc()
    result = dict(ok=False, phase=phase, error=repr(error), samples=samples)
with open(result_path, 'w') as output: json.dump(result, output, indent=2)
print('DESKTOP_WEAPON_WHEEL_RESULT ' + json.dumps(result), flush=True)
try: run('kill') # Avoid normal-exit config writes even in the isolated basedir.
except gdb.error: pass
if not result['ok']: gdb.execute('quit 1')
end
quit 0
