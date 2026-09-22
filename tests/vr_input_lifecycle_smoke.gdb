# Initialized native-key lifecycle proof through VR_InputCommands, Key_Event,
# native bindings, and Cbuf_Execute. It injects only completed action-frame
# fields; it does not manufacture kbutton, usercmd, or packet state.
#
# Run with QSVR_INPUT_BASEDIR pointing to an isolated basedir containing
# id1/pak0.pak, and QSVR_INPUT_LIFECYCLE_RESULT set to a result JSON path:
#   QSVR_INPUT_BASEDIR=/tmp/qsvr-input-profile \
#   QSVR_INPUT_LIFECYCLE_RESULT=/tmp/qsvr-input-lifecycle.json \
#   gdb -q --batch -x tests/vr_input_lifecycle_smoke.gdb \
#     /tmp/quakespasm-2.0-bootstrap-build/vkquake
# The inferior is killed after the probe to avoid normal-exit config writes.
set pagination off
set confirm off
set debuginfod enabled off
python
import gdb, json, os, time

basedir = os.environ.get('QSVR_INPUT_BASEDIR')
result_path = os.environ.get('QSVR_INPUT_LIFECYCLE_RESULT',
                             '/tmp/vr_input_lifecycle_smoke.json')
samples = []
phase = 'startup'
PROFILE_INDEX = None

def integer(expression):
    return int(gdb.parse_and_eval(expression))

def execute(command):
    gdb.execute(command, to_string=True)

def add_command(text):
    execute('call (void)Cbuf_AddText(' + json.dumps(text + '\n') + ')')

def snapshot(label):
    sample = dict(
        label=label,
        attack=integer('(in_attack.state & 1)'),
        jump=integer('(in_jump.state & 1)'),
        dest=integer('key_dest'),
        menu_state=integer('m_state'),
        binding_capture=integer('bind_grab'),
        rtrigger_binding=cstring('keybindings[K_RTRIGGER]'),
        keydown={name: integer('keydown[%s]' % name) for name in (
            'K_LTHUMB', 'K_LTRIGGER', 'K_LTRIGGER_ALT', 'K_RTRIGGER',
            'K_ABUTTON', 'K_XBUTTON', 'K_YBUTTON', 'K_ESCAPE')})
    samples.append(sample)
    return sample

def cstring(expression):
    value = gdb.parse_and_eval(expression)
    return None if int(value) == 0 else value.string()

def set_action(focused=1, left_active=1, right_active=1,
               left_buttons=0, right_buttons=0):
    # Only action-sample fields are populated. The rest remain calloc-zeroed.
    execute('set $vrtest->focused = %d' % focused)
    execute('set $vrtest->hands[0].active = %d' % left_active)
    execute('set $vrtest->hands[1].active = %d' % right_active)
    execute('set $vrtest->hands[0].profile = %d' % PROFILE_INDEX)
    execute('set $vrtest->hands[1].profile = %d' % PROFILE_INDEX)
    execute('set $vrtest->hands[0].pressed = %d' % left_buttons)
    execute('set $vrtest->hands[1].pressed = %d' % right_buttons)

def dispatch(label):
    global phase
    phase = label
    execute('call (void)VR_InputCommands((const vrxr_frame_t *)$vrtest)')
    execute('call (void)Cbuf_Execute()')
    return snapshot(label)

def step(label, focused=1, left_active=1, right_active=1,
         left_buttons=0, right_buttons=0):
    set_action(focused, left_active, right_active, left_buttons, right_buttons)
    return dispatch(label)

def require(condition, detail):
    if not condition:
        raise AssertionError('%s: %s' % (phase, detail))

def no_actions(sample):
    return sample['attack'] == 0 and sample['jump'] == 0

def write_result(value):
    with open(result_path, 'w') as output:
        json.dump(value, output, indent=2)

try:
    if not basedir or not os.path.isfile(os.path.join(basedir, 'id1', 'pak0.pak')):
        raise AssertionError('QSVR_INPUT_BASEDIR must contain id1/pak0.pak')

    start = time.monotonic()
    class Ready(gdb.Breakpoint):
        def stop(self):
            # The callback only reads signon state; inferior calls happen below,
            # after GDB has returned to top level.
            return integer('cls.signon') == 4 or time.monotonic() - start > 90
    ready = Ready('Host_Frame', internal=True)
    gdb.Breakpoint('Host_Error', internal=True)
    gdb.Breakpoint('Sys_Error', internal=True)
    execute('run -basedir "%s" -novr -nosound -window -width 640 -height 480 '
            '+vid_vsync 0 +host_maxfps 144 +map e1m1' % basedir)
    require(gdb.newest_frame().name() == 'Host_Frame' and integer('cls.signon') == 4,
            'did not reach signed-on Host_Frame')
    ready.delete()

    # Host_Init has registered the adapter cvars and called VR_InputInit.
    execute('set $vrtest = (vrxr_frame_t *)calloc(1, sizeof(vrxr_frame_t))')
    require(integer('$vrtest') != 0, 'calloc failed')
    profile_index = integer('VRXR_PROFILE_INDEX')
    globals()['PROFILE_INDEX'] = profile_index
    button_stick = integer('VRXR_BUTTON_STICK')
    button_pad = integer('VRXR_BUTTON_PAD')
    button_primary = integer('VRXR_BUTTON_PRIMARY')
    button_secondary = integer('VRXR_BUTTON_SECONDARY')
    button_trigger = integer('VRXR_BUTTON_TRIGGER')

    # Bind through Quake's native command buffer. LTHUMB acts as the real
    # +altmodifier key; the trigger's release must follow its original ALT map.
    for binding in (
        'bind LTHUMB +altmodifier',
        'bind LTRIGGER +attack',
        'bind LTRIGGER_ALT +jump',
        'bind RTRIGGER +attack',
        'bind ABUTTON +attack',
        'bind XBUTTON +jump',
        'bind YBUTTON +attack'):
        add_command(binding)
    execute('call (void)Cbuf_Execute()')
    execute('call (void)Cvar_SetValue("vr_lefthanded", 0.0)')

    require(integer('key_dest') == integer('key_game'),
            'map did not leave the native input destination in game')
    sample = step('initial_neutral')
    require(no_actions(sample), 'initial neutral sample activated a binding')

    step('alt_modifier_down', left_buttons=button_stick)
    sample = step('alt_trigger_down', left_buttons=button_stick | button_trigger)
    require(sample['jump'] == 1 and sample['attack'] == 0 and
            sample['keydown']['K_LTRIGGER_ALT'] and
            not sample['keydown']['K_LTRIGGER'],
            'trigger did not select its native ALT binding')
    sample = step('alt_modifier_released_while_trigger_held',
                  left_buttons=button_trigger)
    require(sample['jump'] == 1 and sample['keydown']['K_LTRIGGER_ALT'] and
            not sample['keydown']['K_LTHUMB'],
            'modifier change released or remapped the held trigger')
    sample = step('alt_trigger_released', left_buttons=0)
    require(no_actions(sample) and not sample['keydown']['K_LTRIGGER_ALT'],
            'ALT-mapped trigger release did not reach its original native key')

    sample = step('shared_y_down', left_buttons=button_pad, right_buttons=button_pad)
    require(sample['attack'] == 1 and sample['keydown']['K_YBUTTON'],
            'Index pads did not press native YBUTTON')
    sample = step('shared_y_one_hand_released', right_buttons=button_pad)
    require(sample['attack'] == 1 and sample['keydown']['K_YBUTTON'],
            'one Index pad released the other hand\'s YBUTTON contribution')
    sample = step('shared_y_final_release')
    require(no_actions(sample) and not sample['keydown']['K_YBUTTON'],
            'YBUTTON stayed down after its final contributor released')

    sample = step('role_swap_before', left_buttons=button_primary,
                  right_buttons=button_primary)
    require(sample['attack'] == 1 and sample['jump'] == 1,
            'primary bindings were not held before role swap')
    execute('call (void)Cvar_SetValue("vr_lefthanded", 1.0)')
    sample = step('role_swap_held', left_buttons=button_primary,
                  right_buttons=button_primary)
    require(no_actions(sample) and not sample['keydown']['K_ABUTTON'] and
            not sample['keydown']['K_XBUTTON'],
            'held physical buttons survived a logical role swap')
    step('role_swap_neutral')
    sample = step('role_swap_rearmed', left_buttons=button_primary,
                  right_buttons=button_primary)
    require(sample['attack'] == 1 and sample['jump'] == 1,
            'neutral sample did not rearm swapped roles')
    sample = step('role_swap_release')
    require(no_actions(sample), 'swapped primary keys did not release')

    sample = step('missing_hand_before', left_buttons=button_primary,
                  right_buttons=button_primary)
    require(sample['attack'] == 1 and sample['jump'] == 1,
            'missing-hand inputs were not held')
    sample = step('missing_left_hand', left_active=0,
                  left_buttons=button_primary, right_buttons=button_primary)
    require(sample['attack'] == 1 and sample['jump'] == 0 and
            sample['keydown']['K_ABUTTON'] and not sample['keydown']['K_XBUTTON'],
            'missing hand did not release only its owned native key')
    sample = step('missing_hand_still_held', left_buttons=button_primary,
                  right_buttons=button_primary)
    require(sample['attack'] == 1 and sample['jump'] == 0,
            'reactivated held hand bypassed neutral rearm')
    step('missing_hand_neutral')
    sample = step('missing_hand_rearmed', left_buttons=button_primary)
    require(sample['jump'] == 1, 'neutral sample did not rearm the returned hand')
    step('missing_hand_release')

    sample = step('focus_before', right_buttons=button_primary)
    require(sample['attack'] == 1, 'focus test binding was not held')
    sample = step('focus_lost', focused=0, right_buttons=button_primary)
    require(no_actions(sample) and not sample['keydown']['K_ABUTTON'],
            'focus loss did not release native keys')
    sample = step('focus_restored_held', right_buttons=button_primary)
    require(no_actions(sample), 'focus restore bypassed neutral rearm')
    step('focus_restored_neutral')
    sample = step('focus_rearmed', right_buttons=button_primary)
    require(sample['attack'] == 1, 'neutral sample did not rearm after focus loss')
    step('focus_release')

    # Rebinding a held native RTRIGGER must queue the old +attack release
    # before replacing its binding; the new +jump mapping applies next press.
    sample = step('rtrigger_rebind_before', left_buttons=button_trigger)
    require(sample['attack'] == 1 and sample['keydown']['K_RTRIGGER'],
            'RTRIGGER +attack was not held before rebinding')
    add_command('bind RTRIGGER +jump')
    execute('call (void)Cbuf_Execute()')
    sample = snapshot('rtrigger_rebound_while_held')
    require(sample['attack'] == 0 and sample['jump'] == 0 and
            sample['keydown']['K_RTRIGGER'],
            'rebinding did not release old +attack while retaining the held key')
    sample = step('rtrigger_rebound_still_held', left_buttons=button_trigger)
    require(no_actions(sample) and sample['keydown']['K_RTRIGGER'],
            'held RTRIGGER dispatched the new binding without a fresh press')
    sample = step('rtrigger_rebound_release')
    require(no_actions(sample) and not sample['keydown']['K_RTRIGGER'],
            'rebound RTRIGGER did not release cleanly')
    sample = step('rtrigger_new_binding', left_buttons=button_trigger)
    require(sample['jump'] == 1 and sample['attack'] == 0,
            'fresh RTRIGGER press did not use its new +jump binding')
    step('rtrigger_new_binding_release')

    # This is the native modal input grab, distinct from the menu's bind_grab.
    # The completed action sample already contains RTRIGGER when the grab starts.
    execute('set $grabkey = (int *)calloc(1, sizeof(int))')
    execute('set $grabchar = (int *)calloc(1, sizeof(int))')
    set_action(left_buttons=button_trigger)
    execute('call (void)Key_BeginInputGrab()')
    execute('call (void)Key_GetGrabbedInput($grabkey, $grabchar)')
    require(integer('Key_InputGrabActive()') and integer('*$grabkey') == -1,
            'native modal grab did not start empty')
    sample = dispatch('modal_grab_same_sample_trigger')
    execute('call (void)Key_GetGrabbedInput($grabkey, $grabchar)')
    require(integer('Key_InputGrabActive()') and integer('*$grabkey') == -1 and
            no_actions(sample) and not sample['keydown']['K_RTRIGGER'],
            'same-sample RTRIGGER was caught by native modal grab')
    execute('call (void)Key_EndInputGrab()')
    execute('call (void)Cbuf_Execute()')
    sample = step('modal_grab_end_still_held', left_buttons=button_trigger)
    require(no_actions(sample), 'held RTRIGGER bypassed capture neutral rearm')
    step('modal_grab_neutral')
    sample = step('modal_grab_fresh_trigger', left_buttons=button_trigger)
    require(sample['jump'] == 1 and sample['attack'] == 0,
            'fresh RTRIGGER did not resume its configured binding after modal grab')
    step('modal_grab_release')

    execute('call (void)Cvar_SetValue("vr_lefthanded", 0.0)')
    step('clear_test_neutral')
    sample = step('key_clear_before', left_buttons=button_trigger)
    require(sample['attack'] == 1 and sample['keydown']['K_LTRIGGER'],
            'native key was not held before Key_ClearStates')
    execute('call (void)Key_ClearStates()')
    execute('call (void)Cbuf_Execute()')
    sample = snapshot('key_clear_released')
    require(no_actions(sample) and not sample['keydown']['K_LTRIGGER'],
            'Key_ClearStates did not release native key/binding state')
    sample = step('key_clear_held_waits_for_neutral', left_buttons=button_trigger)
    require(no_actions(sample), 'held action bypassed Key_ClearStates neutral gate')
    step('key_clear_neutral')
    sample = step('key_clear_rearmed', left_buttons=button_trigger)
    require(sample['attack'] == 1, 'neutral sample did not rearm after key clear')
    step('key_clear_final_release')

    execute('call (void)Cvar_SetValue("vr_lefthanded", 1.0)')
    step('menu_entry_neutral')
    sample = step('game_escape_with_primary',
                  right_buttons=button_secondary | button_primary)
    require(sample['dest'] == integer('key_menu') and no_actions(sample) and
            not sample['keydown']['K_ABUTTON'],
            'game-to-menu Escape caused same-sample primary clickthrough')
    step('menu_held_waits_for_neutral', right_buttons=button_secondary | button_primary)
    step('menu_neutral')
    sample = step('menu_escape_with_primary',
                  right_buttons=button_secondary | button_primary)
    require(sample['dest'] == integer('key_game') and no_actions(sample) and
            not sample['keydown']['K_ABUTTON'],
            'menu-to-game Escape caused same-sample primary clickthrough')

    # Exercise the actual keys menu and its bind_grab state. A logical-left
    # primary and weapon trigger in one completed sample may change the menu
    # context, but the trailing native key must wait for a fresh neutral edge.
    execute('call (void)Cvar_SetValue("vr_lefthanded", 0.0)')
    step('binding_menu_roles_neutral')
    execute('call (void)M_Menu_Keys_f()')
    require(integer('key_dest') == integer('key_menu'),
            'M_Menu_Keys_f did not enter the native binding menu')
    bind_count = integer('((vec_header_t *)bindnames)[-1].size')
    attack_row = None
    for index in range(bind_count):
        if cstring('bindnames[%d].command' % index) == '+attack':
            attack_row = index
            break
    require(attack_row is not None, 'native key menu has no +attack row')
    execute('set variable keys_cursor = %d' % attack_row)
    require(cstring('bindnames[keys_cursor].command') == '+attack',
            'native key menu cursor did not select +attack')
    sample = step('binding_menu_neutral')
    require(sample['dest'] == integer('key_menu') and no_actions(sample) and
            not integer('M_WaitingForKeyBinding()'),
            'menu-entry neutral sample did not leave binding capture idle')
    prior_trigger_binding = cstring('keybindings[K_RTRIGGER]')
    sample = step('binding_capture_primary_and_trigger_same_sample',
                  left_buttons=button_primary, right_buttons=button_trigger)
    require(sample['dest'] == integer('key_menu') and no_actions(sample) and
            integer('M_WaitingForKeyBinding()') and
            cstring('keybindings[K_RTRIGGER]') == prior_trigger_binding,
            'same-sample primary/trigger did not stop at native bind_grab')
    step('binding_capture_neutral')
    require(integer('M_WaitingForKeyBinding()'),
            'neutral sample unexpectedly exited native bind_grab')
    sample = step('binding_capture_fresh_rtrigger', right_buttons=button_trigger)
    require(sample['dest'] == integer('key_menu') and no_actions(sample) and
            not integer('M_WaitingForKeyBinding()') and
            cstring('keybindings[K_RTRIGGER]') == '+attack' and
            not sample['keydown']['K_RTRIGGER'],
            'fresh native RTRIGGER was not captured into the selected +attack row')

    # No server is active here: a leaked second activation would queue a new
    # map immediately. The actual native menu state must stop at Single Player.
    execute('call (void)CL_Disconnect()')
    execute('call (void)M_Menu_Main_f()')
    execute('set variable m_main_cursor = 0')
    step('submenu_entry_neutral')
    sample = step('submenu_simultaneous_trigger_and_primary',
                  left_buttons=button_primary, right_buttons=button_trigger)
    require(sample['dest'] == integer('key_menu') and
            sample['menu_state'] == integer('m_singleplayer') and
            not integer('sv.active') and integer('cls.state') == integer('ca_disconnected'),
            'single sample crossed a submenu and activated New Game')

    result = dict(ok=True, phase='complete', samples=samples)
    write_result(result)
    print('QSVR_INPUT_LIFECYCLE_PASSED ' + json.dumps(result), flush=True)
except Exception as error:
    result = dict(ok=False, phase=phase, error=repr(error), samples=samples)
    write_result(result)
    print('QSVR_INPUT_LIFECYCLE_FAILED ' + json.dumps(result), flush=True)
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
