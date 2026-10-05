# Native menu lifecycle regression; requires private Xvfb/software Vulkan.
# QSVR_MODLIST_PRIVATE=1 QSVR_MODLIST_RESULT=/path/result.json
# QSVR_MODLIST_USERDIR=/private/user gdb -batch -x this-file DEBUG-ENGINE
set pagination off
set confirm off
set debuginfod enabled off
set print thread-events off
python
import gdb, json, os, subprocess, time

samples = []
phase = 'startup'
def run(cmd): return gdb.execute(cmd, to_string=True)
def iv(expr): return int(gdb.parse_and_eval(expr))
def call(expr): run('call (void)' + expr)
def require(ok, message):
    if not ok: raise AssertionError(phase + ': ' + message)
def text(expr): return gdb.parse_and_eval(expr).string()
def commands(value):
    call('Cbuf_AddText(' + json.dumps(value + '\n') + ')')
    call('Cbuf_Execute()')
def open_mods():
    # Exact menu action used by the main-menu Mods entry (no console alias).
    call('M_Menu_Mods_f()')
def count(vector): return iv('((vec_header_t *)' + vector + ')[-1].size') if iv(vector + ' != 0') else 0
def names(vector):
    result = {}
    for i in range(count(vector)):
        item = vector + '[%d]' % i
        name = text(item + '->name')
        fullname = gdb.parse_and_eval('Modlist_GetFullName(' + item + ')')
        require(name not in result, 'duplicate game-directory entry: ' + name)
        result[name] = fullname.string() if int(fullname) else name
    return result
def snapshot(label):
    global phase
    phase = label
    sample = {'case': label, 'active': text('COM_GetGameNames(0)'),
              'all': names('mods_sorted'), 'filtered': names('mods_filtered'),
              'num_mods': iv('num_mods')}
    require(sample['num_mods'] == len(sample['filtered']), 'filter count mismatch')
    samples.append(sample)
    return sample
class Frame(gdb.Breakpoint):
    def stop(self): return True
def frames(number=3):
    step = Frame('Host_Frame', internal=True)
    try:
        for _ in range(number): run('continue')
    finally: step.delete()
def filter_for(value):
    call('M_Keydown(K_ESCAPE, 0)')
    open_mods()
    for character in value: call('M_Charinput(%d)' % ord(character))
def capture(name):
    frames(4)
    call('GL_SynchronizeEndRenderingTask()')
    subprocess.run(['import', '-display', os.environ['DISPLAY'], '-window', 'root',
                    os.path.join(os.path.dirname(os.environ['QSVR_MODLIST_RESULT']), name)], check=True)

try:
    require(os.environ.get('QSVR_MODLIST_PRIVATE') == '1', 'private-display acknowledgement required')
    start = time.monotonic()
    class Ready(gdb.Breakpoint):
        def stop(self): return iv('cls.signon') == 4 or time.monotonic() - start > 60
    ready = Ready('Host_Frame', internal=True)
    gdb.Breakpoint('Host_Error', internal=True)
    gdb.Breakpoint('Sys_Error', internal=True)
    run('run -basedir ' + json.dumps(os.environ['QSVR_MODLIST_BASEDIR']) +
        ' -userdir ' + json.dumps(os.environ['QSVR_MODLIST_USERDIR']) +
        ' -novr -nosound -window -width 640 -height 480 +map start')
    require(gdb.newest_frame().name() == 'Host_Frame' and iv('cls.signon') == 4, 'stock startup failed')
    ready.delete()
    open_mods()
    initial = snapshot('initial_installed')
    require(len(initial['all']) > 20 and 'immortal' in initial['all'], 'installed assets missing')
    for character in 'the immortal lock': call('M_Charinput(%d)' % ord(character))
    searched = snapshot('search_immortal')
    require(set(searched['filtered']) == {'immortal'}, 'search did not isolate Immortal')
    capture('search-immortal.png')
    call('M_Keydown(K_ENTER, 0)')
    call('Cbuf_Execute()')
    start = time.monotonic()
    ready = Ready('Host_Frame', internal=True)
    run('continue')
    require(gdb.newest_frame().name() == 'Host_Frame' and iv('cls.signon') == 4
            and text('COM_GetGameNames(0)') == 'immortal', 'menu selection did not load Immortal start')
    ready.delete()
    open_mods()
    reopened = snapshot('reopen_after_switch')
    require(set(reopened['all']) == set(initial['all']), 'installed directories disappeared after switch')
    require(reopened['all'] == initial['all'], 'unrelated display names changed after switch')
    require(sum(name == 'The Immortal Lock' for name in reopened['all'].values()) == 1,
            'Immortal name leaked to unrelated mods')
    capture('reopened-installed.png')
    for _ in range(3):
        open_mods()
        refreshed = snapshot('repeat_reopen')
        require(refreshed['all'] == initial['all'], 'repeated scan changed installed names')
    filter_for('peril')
    peril = snapshot('search_other_mod')
    require('peril3.0' in peril['filtered'], 'Peril disappeared from search')
    filter_for('the immortal lock')
    require(set(snapshot('search_immortal_again')['filtered']) == {'immortal'}, 'repeated Immortal search failed')
    result = {'ok': True, 'samples': samples}
except Exception as error:
    result = {'ok': False, 'phase': phase, 'error': repr(error), 'samples': samples}
with open(os.environ['QSVR_MODLIST_RESULT'], 'w') as output: json.dump(result, output, indent=2)
print('MODLIST_SWITCH_RESULT ' + json.dumps({'ok': result['ok'], 'phase': phase}), flush=True)
try: run('kill')
except gdb.error: pass
if not result['ok']: gdb.execute('quit 1')
end
quit 0
