# Live initialized connection reset check. Use only an isolated profile: creates lifecycle.dem.
# QSVR_PEER_ADDRESS defaults to the local pinned diagnostic peer at port 28771.
# QSVR_LIFECYCLE_RESULT selects the JSON result file.
set pagination off
set confirm off
set debuginfod enabled off
python
import gdb, time, json, os, socket
peer=os.environ.get('QSVR_PEER_ADDRESS','127.0.0.1:28771')
phase='private'
deadline=time.monotonic()+45
samples=[]

def integer(e): return int(gdb.parse_and_eval(e))
def state(label):
    return dict(label=label,legacy=integer('cls.legacy_qsvr'),dialect=integer('cl.protocol_qsvr'),signon=integer('cls.signon'),connection=integer('cls.state'),demo=integer('cls.demoplayback'),local=integer('sv.active'))
class Frame(gdb.Breakpoint):
    def stop(self):
        if time.monotonic()>deadline: return True
        if phase in ('private','local','private_again'):
            return integer('cls.signon')==4
        if phase=='disconnected':
            return integer('cls.state')==integer('ca_disconnected')
        if phase=='recorded':
            return time.monotonic()>record_until
        if phase=='demo':
            return integer('cls.demoplayback')!=0 and integer('cls.signon')==4
        return False
Frame('Host_Frame',internal=True)
err=gdb.Breakpoint('Host_Error',internal=True)
gdb.Breakpoint('Sys_Error',internal=True)

def resume():
    global deadline
    deadline=time.monotonic()+45
    gdb.execute('continue')
    # Debug builds deliberately trap inside Host_Error before native teardown.
    if phase == 'disconnected' and gdb.newest_frame().name() != 'Host_Frame':
        assert integer('$_siginfo.si_signo') == 5
        gdb.execute('continue')
def command(text):
    gdb.execute('call (void)Cbuf_AddText('+json.dumps(text+'\n')+')',to_string=True)
def check(label,private=False,dialect=None):
    assert gdb.newest_frame().name()=='Host_Frame'
    s=state(label); samples.append(s)
    if dialect is None: dialect=int(private)
    assert s['signon']==4 and s['legacy']==int(private) and s['dialect']==dialect,s

try:
    gdb.execute('run')
    check('initial_private',True)
    phase='public_error'
    command('connect '+peer)
    resume()
    assert gdb.newest_frame().name()=='Host_Error'
    s=state('public_rejects_private_header'); samples.append(s)
    assert s['legacy']==0 and s['dialect']==0,s
    phase='disconnected'; resume()
    assert gdb.newest_frame().name()=='Host_Frame'
    s=state('after_public_error'); samples.append(s)
    assert s['legacy']==0 and s['dialect']==0 and s['signon']==0 and s['connection']==integer('ca_disconnected'),s
    # A bound, silent local UDP socket guarantees this is not another server.
    silent=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
    silent.bind(('127.0.0.1',0))
    phase='failed_private'; command('connect 127.0.0.1:%d qsvr1' % silent.getsockname()[1]); resume()
    assert gdb.newest_frame().name()=='Host_Error'
    assert 'connect failed' in gdb.parse_and_eval('error').string()
    phase='disconnected'; resume()
    s=state('after_failed_private_connect'); samples.append(s)
    assert s['legacy']==0 and s['dialect']==0 and s['connection']==integer('ca_disconnected'),s
    silent.close()
    phase='private_again'; command('connect '+peer+' qsvr1'); resume()
    check('private_reconnected',True)
    phase='local'; command('map e1m1'); resume()
    local_private=int(float(gdb.parse_and_eval('sv_qsvr_private.value'))!=0)
    check('local_after_private',False,local_private)
    assert integer('sv.active')
    command('record lifecycle')
    phase='recorded'; record_until=time.monotonic()+1; resume()
    assert integer('cls.demorecording')
    command('stop')
    phase='recorded'; record_until=time.monotonic()+.1; resume()
    assert not integer('cls.demorecording')
    phase='demo'; command('playdemo lifecycle'); resume()
    check('private_demo' if local_private else 'public_demo',False,local_private)
    assert integer('cls.demoplayback')
    print('QSVR_LIFECYCLE_PASSED '+json.dumps(samples))
finally:
    with open(os.environ['QSVR_LIFECYCLE_RESULT'],'w') as f: json.dump(samples,f,indent=2)
end
quit
