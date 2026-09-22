# Requires isolated simulated Monado, an initialized -openxr client, and stock assets.
# Only controller actions are injected. Actual XR frame refresh, native input grab,
# and SCR_ModalMessage run normally. Not physical-controller or HMD qualification.
set pagination off
set confirm off
set debuginfod enabled off
set print thread-events off
python
import gdb, time, json, os
started=time.monotonic()
records=[]
begins=0
calls=0
case=''
expected_alt=False
def integer(e): return int(gdb.parse_and_eval(e))
class Ready(gdb.Breakpoint):
    def stop(self):
        return time.monotonic()-started>45 or (integer('cls.signon')==4 and integer('vulkan_globals.stereo_active'))
ready=Ready('Host_Frame',internal=True)
gdb.Breakpoint('Host_Error',internal=True)
gdb.Breakpoint('Sys_Error',internal=True)
gdb.execute('run')
assert gdb.newest_frame().name()=='Host_Frame' and integer('vulkan_globals.stereo_active') and integer('cls.signon')==4
ready.delete()
gdb.execute('set $modalframe = (vrxr_frame_t *)calloc(1,sizeof(vrxr_frame_t))',to_string=True)
gdb.execute('set $modalframe->focused = 1',to_string=True)
for hand in range(2):
    gdb.execute('set $modalframe->hands[%d].active = 1'%hand,to_string=True)
    gdb.execute('set $modalframe->hands[%d].profile = VRXR_PROFILE_VIVE'%hand,to_string=True)
class Begin(gdb.Breakpoint):
    def stop(self):
        global begins
        begins+=1
        return False
class Input(gdb.Breakpoint):
    def stop(self):
        global calls
        assert integer('key_inputgrab.active')
        calls+=1
        gdb.execute('set variable frame = $modalframe',to_string=True)
        gdb.execute('set $modalframe->hands[1].pressed = 0',to_string=True)
        gdb.execute('set $modalframe->hands[1].trigger = 0',to_string=True)
        for hand in range(2):
            gdb.execute('set $modalframe->hands[%d].stick[1] = 0'%hand,to_string=True)
        if expected_alt: assert integer('joy_altmodifier_pressed')

        # The trigger held at entry must be ignored; only a release then a
        # fresh press may confirm. No host-frame advancement is available.
        if case in ('yes','yes_with_noise','alt_yes') and (calls<=2 or calls>=5):
            gdb.execute('set $modalframe->hands[1].trigger = 1',to_string=True)
        elif case in ('no','no_with_noise','alt_no') and calls>=3:
            gdb.execute('set $modalframe->hands[1].pressed = VRXR_BUTTON_MENU',to_string=True)
        if case=='yes_with_noise' and calls>=5:
            gdb.execute('set $modalframe->hands[1].pressed = VRXR_BUTTON_GRIP | VRXR_BUTTON_PAD | VRXR_BUTTON_PRIMARY',to_string=True)
            gdb.execute('set $modalframe->hands[1].stick[1] = 1',to_string=True)
        elif case=='no_with_noise' and calls>=3:
            gdb.execute('set $modalframe->hands[1].pressed = VRXR_BUTTON_MENU | VRXR_BUTTON_GRIP | VRXR_BUTTON_PAD',to_string=True)
            gdb.execute('set $modalframe->hands[1].trigger = 1',to_string=True)
            gdb.execute('set $modalframe->hands[1].stick[1] = 1',to_string=True)
        return False
begin_probe=Begin('VRXR_BeginFrame',internal=True)
input_probe=Input('VR_InputCommands',internal=True)
for case in ('yes','no','timeout','yes_with_noise','no_with_noise','alt_yes','alt_no'):
    calls=0
    expected_alt=case.startswith('alt_')
    if expected_alt:
        gdb.execute('call (void)Cbuf_AddText('+json.dumps('bind LTHUMB +altmodifier\nbind ABUTTON_ALT +jump\nbind BBUTTON_ALT +attack\n')+')',to_string=True)
        gdb.execute('call (void)Cbuf_Execute()',to_string=True)
        gdb.execute('call (void)Key_Event(K_LTHUMB,1)',to_string=True)
        gdb.execute('call (void)Cbuf_Execute()',to_string=True)
        assert integer('joy_altmodifier_pressed')
    old_begins=begins
    host=integer('host_framecount')
    limit=.25 if case=='timeout' else 3.0
    gdb.execute('set $modalresult = (int)SCR_ModalMessage("Controller modal probe",%f)'%limit,to_string=True)
    record=dict(case=case,result=integer('$modalresult'),input_calls=calls,xr_begins=begins-old_begins,host_unchanged=host==integer('host_framecount'),grab_released=not integer('key_inputgrab.active'))
    records.append(record)
    assert record['result']==int(case in ('yes','yes_with_noise','alt_yes')),record
    assert record['host_unchanged'] and record['grab_released'] and record['xr_begins']>=record['input_calls']>=3,record
    if case in ('yes','yes_with_noise','alt_yes'): assert calls>=5,record
    if expected_alt:
        # Modal entry queued the native modifier release; its command remains
        # pending until the blocking modal returns. Do not clear the bit by hand.
        assert integer('joy_altmodifier_pressed')
        gdb.execute('call (void)Cbuf_Execute()',to_string=True)
        assert not integer('joy_altmodifier_pressed')
        assert not integer('in_attack.state & 1') and not integer('in_jump.state & 1')
        record['native_modifier_released']=True
# Verify the actual loading early return invalidates input without touching
# the completed pose metadata or trying to begin another XR frame.
for bp in (input_probe, begin_probe): bp.enabled=False
gdb.execute('set openxr_frame.focused = 1',to_string=True)
gdb.execute('set openxr_frame.hands[0].active = 1',to_string=True)
gdb.execute('set openxr_frame.hands[0].pressed = VRXR_BUTTON_PRIMARY',to_string=True)
pose=float(gdb.parse_and_eval('openxr_frame.devices[0].matrix[1][3]'))
gdb.execute('set scr_disabled_for_loading = 1',to_string=True)
gdb.execute('set scr_disabled_time = realtime',to_string=True)
gdb.execute('call (void)SCR_UpdateScreen(0)',to_string=True)
assert not integer('openxr_frame.focused') and not integer('openxr_frame.hands[0].active') and not integer('openxr_frame.hands[0].pressed')
assert pose==float(gdb.parse_and_eval('openxr_frame.devices[0].matrix[1][3]'))
assert not integer('in_update_screen')
gdb.execute('set scr_disabled_for_loading = 0',to_string=True)
records.append(dict(case='loading_invalidates_input_preserves_pose',passed=True))
with open(os.environ['QSVR_MODAL_RESULT'],'w') as f: json.dump(records,f,indent=2)
print('QSVR_INPUT_MODAL_PASSED '+json.dumps(records))
end
quit
