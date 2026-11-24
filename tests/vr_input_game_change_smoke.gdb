# In-process game change with an attached XR session, followed by controller
# use in the menu and in gameplay. It drives the native COM_SwitchGame path
# (as a server-requested game change does), runs the new game's configuration
# chain through Cbuf_Execute while injected completed XR samples arrive, and
# then checks that the dedicated XR controls are bound and that the trigger
# selects in the menu and fires in game. Only completed action-frame fields
# are injected; bindings, menus, kbuttons and the server are the real engine.
#
# The new game mimics a mod whose quake.rc execs default.cfg (unbindall plus
# vr_defaultbindings), a config.cfg that starts with unbindall and has no XR
# binds, autoexec.cfg and stuffcmds, then opens the main menu. A post-config
# that also starts with unbindall and has no XR binds is passed with -postcfg.
#
# QSVR_INPUT_BASEDIR must be a disposable basedir containing id1/pak0.pak; the
# probe writes the test game directory and post-config into it.
#   QSVR_INPUT_BASEDIR=/tmp/qsvr-input-profile \
#   QSVR_INPUT_GAME_CHANGE_RESULT=/tmp/qsvr-input-game-change.json \
#   gdb -nx --batch -x tests/vr_input_game_change_smoke.gdb build/vkquake
# Run it on a hidden display with OpenXR blocked (see tests/README.md).
set pagination off
set confirm off
set debuginfod enabled off
python
import gdb, json, os, time

basedir = os.environ.get('QSVR_INPUT_BASEDIR')
result_path = os.environ.get('QSVR_INPUT_GAME_CHANGE_RESULT',
                             '/tmp/vr_input_game_change_smoke.json')
GAME = 'qsvr_gamechange'
samples = []
phase = 'startup'
sample_id = 0

def integer(expression):
    return int(gdb.parse_and_eval(expression))

def execute(command):
    return gdb.execute(command, to_string=True)

def cstring(expression):
    value = gdb.parse_and_eval(expression)
    return None if int(value) == 0 else value.string()

def add_command(text):
    execute('call (void)Cbuf_AddText(' + json.dumps(text + '\n') + ')')

def require(condition, detail):
    if not condition:
        raise AssertionError('%s: %s' % (phase, detail))

def write_result(value):
    with open(result_path, 'w') as output:
        json.dump(value, output, indent=2)

def set_action(left_buttons=0, right_buttons=0, right_trigger=0.0):
    global sample_id
    sample_id += 1
    execute('set $vrtest->sample_id = %d' % sample_id)
    execute('set $vrtest->focused = 1')
    for hand, buttons in ((0, left_buttons), (1, right_buttons)):
        execute('set $vrtest->hands[%d].active = 1' % hand)
        execute('set $vrtest->hands[%d].profile = VRXR_PROFILE_INDEX' % hand)
        execute('set $vrtest->hands[%d].pressed = %d' % (hand, buttons))
        execute('set $vrtest->hands[%d].trigger = %f' % (hand, 0.0))
    execute('set $vrtest->hands[1].trigger = %f' % right_trigger)

def snapshot(label):
    sample = dict(
        label=label,
        attack=integer('(in_attack.state & 1)'),
        jump=integer('(in_jump.state & 1)'),
        dest=integer('key_dest'),
        menu_state=integer('m_state'),
        rtrigger=integer('keydown[K_VR_RTRIGGER]'),
        rtrigger_binding=cstring('keybindings[K_VR_RTRIGGER]'),
        abutton_binding=cstring('keybindings[K_VR_ABUTTON]'))
    samples.append(sample)
    return sample

def step(label, menu_panel=False, **action):
    global phase
    phase = label
    set_action(**action)
    execute('call (void)VR_InputCommands((const vrxr_frame_t *)$vrtest)')
    execute('call (void)Cbuf_Execute()')
    if menu_panel:
        # The post-draw menu phase; no panel was drawn without a headset.
        execute('call (void)VR_InputMenuPanelTrigger((const vrxr_frame_t *)$vrtest, 0)')
        execute('call (void)Cbuf_Execute()')
    return snapshot(label)

# Dedicated XR controls. The first game's configs bind none of them, so its
# first XR sample leaves exactly the filled defaults; the defaults table itself
# is optimized out of release builds.
XR_KEYS = ('K_VR_LTRIGGER', 'K_VR_RTRIGGER', 'K_VR_ABUTTON', 'K_VR_BBUTTON',
           'K_VR_XBUTTON', 'K_VR_YBUTTON', 'K_VR_LTHUMB', 'K_VR_RTHUMB',
           'K_VR_LSHOULDER', 'K_VR_RSHOULDER', 'K_VR_ALTFIRE')
filled_defaults = {}

def xr_bindings():
    return {name: cstring('keybindings[%s]' % name) for name in XR_KEYS}

def require_defaults_filled(detail):
    current = xr_bindings()
    require(current == filled_defaults,
            '%s: XR bindings %r, expected the defaults %r' % (detail, current, filled_defaults))

def wait_for_signon(label):
    global phase
    phase = label
    start = time.monotonic()
    class Ready(gdb.Breakpoint):
        def stop(self):
            return integer('cls.signon') == 4 or time.monotonic() - start > 90
    ready = Ready('Host_Frame', internal=True)
    execute('continue')
    ready.delete()
    require(gdb.newest_frame().name() == 'Host_Frame' and integer('cls.signon') == 4,
            'did not reach a signed-on Host_Frame')

try:
    if not basedir or not os.path.isfile(os.path.join(basedir, 'id1', 'pak0.pak')):
        raise AssertionError('QSVR_INPUT_BASEDIR must contain id1/pak0.pak')
    game_dir = os.path.join(basedir, GAME)
    os.makedirs(game_dir, exist_ok=True)
    with open(os.path.join(game_dir, 'quake.rc'), 'w') as f:
        f.write('exec default.cfg\nexec config.cfg\nbind "i" "impulse 175"\n'
                'exec autoexec.cfg\nstuffcmds\nmenu_main\n')
    with open(os.path.join(game_dir, 'config.cfg'), 'w') as f:
        f.write('unbindall\nbind "w" "+forward"\nbind "MOUSE1" "+attack"\n'
                'bind "RTRIGGER" "+attack"\nbind "ABUTTON" "+jump"\n')
    postcfg = os.path.join(basedir, 'qsvr_gamechange_post.cfg')
    with open(postcfg, 'w') as f:
        f.write('unbindall\nbind "w" "+forward"\nbind "SPACE" "+jump"\n')

    start = time.monotonic()
    class Ready(gdb.Breakpoint):
        def stop(self):
            return integer('cls.signon') == 4 or time.monotonic() - start > 90
    ready = Ready('Host_Frame', internal=True)
    gdb.Breakpoint('Host_Error', internal=True)
    gdb.Breakpoint('Sys_Error', internal=True)
    execute('run -basedir "%s" -novr -nosound -window -width 640 -height 480 '
            '-postcfg "%s" +vid_vsync 0 +host_maxfps 144 +map start' % (basedir, postcfg))
    require(gdb.newest_frame().name() == 'Host_Frame' and integer('cls.signon') == 4,
            'did not reach signed-on Host_Frame')
    ready.delete()

    execute('set $vrtest = (vrxr_frame_t *)calloc(1, sizeof(vrxr_frame_t))')
    require(integer('$vrtest') != 0, 'calloc failed')
    trigger_bit = integer('VRXR_BUTTON_TRIGGER')
    secondary_bit = integer('VRXR_BUTTON_SECONDARY')
    execute('call (void)Cvar_SetValue("vr_lefthanded", 0.0)')

    # The previous game settles: its first XR sample fills the defaults.
    step('first_game_sample')
    filled_defaults.update(xr_bindings())
    require(filled_defaults['K_VR_RTRIGGER'] and filled_defaults['K_VR_LTRIGGER'] and
            filled_defaults['K_VR_ABUTTON'] and filled_defaults['K_VR_BBUTTON'],
            'first game did not fill the XR defaults: %r' % filled_defaults)

    # Server-requested game change while the XR session stays attached.
    # An attached session keeps V_TrackedSessionActive true while the chain's
    # default.cfg runs vr_defaultbindings. Borrow only the session flag, and only
    # inside that command, so no renderer path runs without a real session.
    phase = 'game_change'
    session_requests = []
    class SessionDuringDefaults(gdb.Breakpoint):
        def stop(self):
            session_requests.append(1)
            gdb.execute('set variable vulkan_globals.stereo_active = 1', to_string=True)
            return False
    class SessionEnd(gdb.Breakpoint):
        def stop(self):
            gdb.execute('set variable vulkan_globals.stereo_active = 0', to_string=True)
            return False
    session_begin = SessionDuringDefaults('VR_InputDefaultBindings_f', internal=True)
    session_end = SessionEnd('Cmd_ExecuteString', internal=True)
    execute('call (void)COM_SwitchGame("id1;%s")' % GAME)
    for _ in range(64):
        if not integer('cmd_text.cursize'):
            break
        # A completed XR sample can arrive between chain commands.
        set_action()
        execute('call (void)VR_InputCommands((const vrxr_frame_t *)$vrtest)')
        execute('call (void)Cbuf_Execute()')
        execute('set variable vulkan_globals.stereo_active = 0')
        execute('call (void)Cbuf_Waited()')
    require(not integer('cmd_text.cursize'), 'configuration chain did not finish')
    require(GAME in (cstring('(char *)com_gamenames') or ''),
            'game directory did not change')
    session_begin.delete()
    session_end.delete()
    execute('set variable vulkan_globals.stereo_active = 0')
    require(session_requests, 'default.cfg did not request the VR defaults')

    sample = step('after_chain_sample')
    require_defaults_filled('after game change')
    require(cstring('keybindings[K_SPACE]') == '+jump' and
            not cstring('keybindings[K_RTRIGGER]'),
            'post-config did not run last in the chain')

    # quake.rc opened the main menu: the trigger selects with a fresh press.
    require(sample['dest'] == integer('key_menu') and
            sample['menu_state'] == integer('m_main'),
            'new game did not open its main menu')
    execute('set variable m_main_cursor = 0')
    step('menu_neutral', menu_panel=True)
    step('menu_trigger_down', menu_panel=True, right_trigger=1.0)
    sample = step('menu_trigger_up', menu_panel=True)
    require(sample['dest'] == integer('key_menu') and
            sample['menu_state'] == integer('m_singleplayer'),
            'trigger did not select in the main menu after the game change')
    sample = step('menu_back_down', left_buttons=secondary_bit)
    step('menu_back_up')

    # Reconnect, then the trigger fires through its filled binding.
    add_command('map start')
    wait_for_signon('reconnect')
    for _ in range(4):
        if integer('key_dest') != integer('key_menu'):
            break
        add_command('togglemenu')
        execute('call (void)Cbuf_Execute()')
    require(integer('key_dest') == integer('key_game'), 'menu did not close')
    step('game_neutral')
    sample = step('game_trigger_down', right_trigger=1.0)
    require(sample['attack'] == 1 and sample['rtrigger'] == 1,
            'trigger did not fire after the game change')
    sample = step('game_trigger_up')
    require(sample['attack'] == 0 and sample['rtrigger'] == 0,
            'trigger did not release')
    sample = step('game_trigger_click', right_buttons=trigger_bit)
    require(sample['attack'] == 1, 'trigger click did not fire')
    step('game_trigger_click_up')

    result = dict(ok=True, phase='complete', samples=samples)
    write_result(result)
    print('QSVR_INPUT_GAME_CHANGE_PASSED ' + json.dumps(result), flush=True)
except Exception as error:
    result = dict(ok=False, phase=phase, error=repr(error), samples=samples)
    write_result(result)
    print('QSVR_INPUT_GAME_CHANGE_FAILED ' + json.dumps(result), flush=True)
    try:
        execute('kill')
    except gdb.error:
        pass
    gdb.execute('quit 1')

try:
    execute('kill')
except gdb.error:
    pass
end
quit 0
