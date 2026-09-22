# Run against the Linux debugoptimized executable, before engine initialization.
# QSVR_SERVERINFO_CASE: fit, oversized, unterminated. No game assets needed.
set pagination off
set confirm off
set debuginfod enabled off
break main
run -novr
python
import os
import gdb

case = os.environ['QSVR_SERVERINFO_CASE']
assert case in ('fit', 'oversized', 'unterminated')
prefix = b'//fullserverinfo "'
head = b'\\metadata\\'
tail = b'\\sv_gravity\\600'
suffix = b'"\n'
length = 2048 if case == 'oversized' else 2047
if case == 'unterminated':
    length = 200
info = head + b'x' * (length - len(prefix + head + tail + suffix)) + tail
command = prefix + info + suffix
assert len(command) == length
packet = bytes([9]) + command + (b'' if case == 'unterminated' else b'\0')
gdb.execute('set $packet = (byte *)Mem_Alloc(8192)')
gdb.selected_inferior().write_memory(int(gdb.parse_and_eval('$packet')), packet)
gdb.execute('set net_message.data = $packet')
gdb.execute('set net_message.cursize = %d' % len(packet))
gdb.execute('set net_message.maxsize = 8192')
gdb.execute('call (void)Cmd_AddCommand2("fullserverinfo", CL_ServerExtension_FullServerinfo_f, src_server, 0)')

class Callback(gdb.Breakpoint):
    hits = 0
    def stop(self):
        self.hits += 1
        return False

callback = Callback('*CL_ServerExtension_FullServerinfo_f', internal=True)
error = gdb.Breakpoint('Host_Error', internal=True)
try:
    gdb.execute('call (void)CL_ParseServerMessage()')
except gdb.error:
    # GDB interrupts an inferior call when the actual Host_Error entry is hit.
    if case == 'fit' or gdb.newest_frame().name() != 'Host_Error':
        raise
    assert 'truncated server command' in gdb.parse_and_eval('error').string()
    assert callback.hits == 0
else:
    assert case == 'fit', 'oversized/unterminated command was dispatched'
    assert callback.hits == 1, callback.hits
    actual = gdb.parse_and_eval('cl.serverinfo').string().encode()
    assert actual == info, (len(actual), len(info), actual[:20], info[:20], actual[-25:], info[-25:])
    assert int(gdb.parse_and_eval('PMCL_SetMoveVars()')) == 1
    assert float(gdb.parse_and_eval('movevars.gravity')) == 600.0
print('SERVERINFO_COMMAND_%s_PASSED' % case)
end
quit 0
