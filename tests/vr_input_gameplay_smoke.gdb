# Initialized local gameplay proof of the action -> native binding -> command path.
# Injects at VR_InputCommands, NOT kbutton/usercmd/packet fields. No HMD/runtime proof.
# Run only with an isolated basedir, +map e1m1, and QSVR_INPUT_RESULT set.
set pagination off
set confirm off
set debuginfod enabled off
python
import gdb, time, json, os
start=time.monotonic()
phase='loading'
samples=[]

def integer(expr): return int(gdb.parse_and_eval(expr))
def vector(expr): return [float(gdb.parse_and_eval('%s[%d]'%(expr,i))) for i in range(3)]
def sample(label):
    return dict(label=label,origin=vector('cl.entities[cl.viewentity].netstate.origin'),
                shells=integer('cl.stats[6]'),attack=integer('in_attack.state'),
                forward=integer('in_forward.state'),dest=integer('key_dest'),
                sent=integer('cl.movemessages'))
class Ready(gdb.Breakpoint):
    def stop(self):
        return time.monotonic()-start>45 or integer('cls.signon')==4
ready=Ready('Host_Frame',internal=True)
gdb.Breakpoint('Host_Error',internal=True)
gdb.Breakpoint('Sys_Error',internal=True)
gdb.execute('run')
assert gdb.newest_frame().name()=='Host_Frame' and integer('cls.signon')==4
ready.delete()
# Inferior calls are made outside Breakpoint.stop: command execution remains native.
gdb.execute('set $vrtest = (vrxr_frame_t *)calloc(1, sizeof(vrxr_frame_t))',to_string=True)
assert integer('$vrtest')
gdb.execute('set $vrtest->focused = 1',to_string=True)
gdb.execute('set $vrtest->hands[0].active = 1',to_string=True)
gdb.execute('set $vrtest->hands[1].active = 1',to_string=True)
gdb.execute('set $vrtest->hands[0].profile = VRXR_PROFILE_INDEX',to_string=True)
gdb.execute('set $vrtest->hands[1].profile = VRXR_PROFILE_INDEX',to_string=True)
gdb.execute('call (void)Cbuf_AddText('+json.dumps('bind VR_ABUTTON +forward\nbind VR_RTRIGGER +attack\n')+')',to_string=True)
phase='neutral'
phase_time=time.monotonic()
class Input(gdb.Breakpoint):
    def stop(self):
        global phase,phase_time
        now=time.monotonic()
        if now-start>60: return True
        # Only the OpenXR action boundary is simulated. Native host/key/Cbuf,
        # CL_BaseMove/FinishMove, transport, server and QuakeC run normally.
        gdb.execute('set variable frame = $vrtest',to_string=True)
        if phase=='neutral' and now-phase_time>.5:
            samples.append(sample('before'))
            gdb.execute('set $vrtest->hands[0].pressed = VRXR_BUTTON_PRIMARY',to_string=True)
            gdb.execute('set $vrtest->hands[1].trigger = 1',to_string=True)
            phase,phase_time='held',now
        elif phase=='held' and now-phase_time>1.5:
            samples.append(sample('held'))
            gdb.execute('set $vrtest->focused = 0',to_string=True)
            phase,phase_time='focus_lost',now
        elif phase=='focus_lost' and now-phase_time>.75:
            samples.append(sample('released'))
            gdb.execute('set $vrtest->focused = 1',to_string=True)
            phase,phase_time='held_after_focus',now
        elif phase=='held_after_focus' and now-phase_time>.75:
            samples.append(sample('held_after_focus'))
            gdb.execute('set $vrtest->hands[0].pressed = 0',to_string=True)
            gdb.execute('set $vrtest->hands[1].trigger = 0',to_string=True)
            phase,phase_time='rearm',now
        elif phase=='rearm' and now-phase_time>.2:
            gdb.execute('set $vrtest->hands[1].trigger = 1',to_string=True)
            phase,phase_time='pressed_again',now
        elif phase=='pressed_again' and now-phase_time>.75:
            samples.append(sample('pressed_again'))
            phase='done'
            return True
        return False
Input('VR_InputCommands',internal=True)
gdb.execute('continue')
result=dict(phase=phase,samples=samples,stop=gdb.newest_frame().name())
with open(os.environ['QSVR_INPUT_RESULT'],'w') as f: json.dump(result,f,indent=2)
assert phase=='done',result
before,held,released,after,again=samples
assert held['shells']<before['shells'],result
assert sum((a-b)**2 for a,b in zip(held['origin'],before['origin']))>16**2,result
assert held['attack']&1 and held['forward']&1,result
assert not released['attack']&1 and not released['forward']&1,result
assert after['shells']==released['shells'] and not after['attack']&1 and not after['forward']&1,result
assert again['shells']<after['shells'] and again['attack']&1,result
print('QSVR_INPUT_GAMEPLAY_PASSED '+json.dumps(result))
end
quit
