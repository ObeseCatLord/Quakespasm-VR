# Native custom binding qualification; use run_custom_mod_bindings_native.py.
# Requires a Debug engine and locally owned Sacrilege/id1 assets.
# Dispatches real menu/key/alias/save owners; does not certify XR pointer hardware.
set pagination off
set confirm off
set breakpoint pending on
set print thread-events off
set debuginfod enabled off
python
import gdb,os,json,time
started=time.monotonic()
def val(e): return gdb.parse_and_eval(e)
def num(e): return int(val(e))
def call(e): return val(e)
def setv(e,v):gdb.execute('set variable %s = %s'%(e,v),to_string=True)
def string(e):return val(e).string()
def q(s):return json.dumps(s)
def command(s):
 data=(s+'\n').encode();n=num('cmd_text.cursize');assert n+len(data)<num('cmd_text.maxsize')
 gdb.selected_inferior().write_memory(num('(long)cmd_text.data')+n,data);setv('cmd_text.cursize',n+len(data));call('Cbuf_Execute()')
class NoFocus(gdb.Breakpoint):
 def stop(self):gdb.execute('return (int)0',to_string=True);return False
NoFocus('SDL_RaiseWindow',internal=True)
NoFocus('SDL_SetWindowInputFocus',internal=True)
class Ready(gdb.Breakpoint):
 def stop(self):return time.monotonic()-started>45 or (num('cls.signon')==4 and num('cl.worldmodel != 0'))
b=Ready('Host_Frame',internal=True)
gdb.execute('run');b.enabled=False
assert num('cls.signon')==4
assert num('Cmd_AliasExists("+hook")') and num('Cmd_AliasExists("-hook")')
assert not num('Cmd_Exists("vr_migrate_mod_bindings")')
mode=os.environ['QSVR_CUSTOM_PHASE'];longcmd='echo '+'x'*250
vkey=num('K_VR_ALTFIRE');lkey=ord('k')
def binding(k):return string('keybindings[%d]'%k) if num('keybindings[%d]!=0'%k) else ''
def openmenu():call('M_Menu_Keys_f()')
def enter(text):
 call('M_Keys_BeginCommand()')
 for c in text:call('M_Keys_CommandChar(%d)'%ord(c))
 call('M_Keys_SubmitCommand()')
def capture(k):
 setv('in_impulse',0);call('Key_Event(%d,1)'%k);assert not num('M_WaitingForKeyBinding()')
 assert not num('keydown[%d]'%k);call('Key_Event(%d,0)'%k);call('Cbuf_Execute()');assert num('in_impulse')==0
shots=[]
def screenshot(label):
 command('screenshot png');filename=string('screenshot_imagename')
 call('SCR_UpdateScreen(0)');call('GL_SynchronizeEndRenderingTask()')
 shots.append(dict(label=label,path=string('com_gamedir')+'/'+filename))
if mode=='save':
 assert binding(vkey)=='+button3',binding(vkey)
 openmenu();assert not num('M_WaitingForKeyBinding()')
 if os.environ.get('QSVR_CUSTOM_SCREENSHOTS'):
  screenshot('controls')
  call('M_Keys_BeginCommand()');call('M_Keys_CommandChar(43)');screenshot('editor');openmenu()
 # Reconstructed saved commands and cancellation keep the authoritative keys.
 for k in (ord('x'),ord('y'),ord('z')):call('Key_SetBinding(%d,"+hook")'%k)
 openmenu();call('M_Keys_SelectCommand("+hook")');call('M_Keys_Key(%d)'%num('K_ENTER'))
 assert num('M_WaitingForKeyBinding()');call('M_Keys_Key(%d)'%num('K_ESCAPE'))
 assert all(binding(k)=='+hook' for k in (ord('x'),ord('y'),ord('z')))
 # Interruption/reopening clears stale capture; no assignment was made.
 openmenu();call('M_Keys_SelectCommand("+hook")');call('M_Keys_Key(%d)'%num('K_ENTER'))
 setv('key_dest','key_console');assert not num('M_VRPointerBindingGrab()')
 openmenu();assert not num('M_WaitingForKeyBinding()')
 enter('  +hook  ');assert num('M_WaitingForKeyBinding()');capture(vkey)
 assert binding(vkey)=='+hook' and all(not binding(k) for k in (ord('x'),ord('y'),ord('z')))
 # Real key dispatch expands the actual mod's aliases to press and release impulses.
 setv('key_dest','key_game');setv('in_impulse',0)
 call('Key_Event(%d,1)'%vkey);call('Cbuf_Execute()');assert num('in_impulse')==24
 call('Key_Event(%d,0)'%vkey);call('Cbuf_Execute()');assert num('in_impulse')==25
 openmenu();enter(longcmd);assert num('M_WaitingForKeyBinding()');capture(lkey);assert binding(lkey)==longcmd
 call('Host_WriteConfiguration()')
else:
 assert binding(vkey)=='+hook',binding(vkey)
 assert binding(lkey)==longcmd,binding(lkey)
 openmenu();call('M_Keys_SelectCommand("+hook")')
 assert string('bindnames[keys_cursor].command')=='+hook'
 command('game id1')
 assert binding(vkey)=='+hook' # No special-case stale-hook rewrite.
 command('game sacrilege')
 assert binding(vkey)=='+hook'
result=dict(screenshots=shots,status='passed',phase=mode,gamedir=string('com_gamedir'),hook_binding=binding(vkey),long_binding_bytes=len(binding(lkey)),mod_alias_press_release=mode=='save')
with open(os.environ['QSVR_CUSTOM_RESULT'],'w') as f:json.dump(result,f)
print('CUSTOM_BINDINGS_NATIVE_PASSED '+json.dumps(result),flush=True)
gdb.execute('kill',to_string=True)
end
quit
