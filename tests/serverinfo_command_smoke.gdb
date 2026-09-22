# Run against the Linux debugoptimized executable, before engine initialization.
# QSVR_SERVERINFO_CASE: fit, beyond2k, capacity, oversized, unterminated,
# comment_line, comment_block, token_overflow, token_overflow_quoted, ordinary_oversized.
# No game assets needed; token-overflow cases dispatch directly through Cmd_ExecuteString.
set pagination off
set confirm off
set debuginfod enabled off
break main
run -novr
python
import os
import gdb

case = os.environ['QSVR_SERVERINFO_CASE']
success_cases = ('fit', 'beyond2k', 'capacity', 'comment_line', 'comment_block')
wire_errors = ('oversized', 'unterminated', 'ordinary_oversized')
token_errors = ('token_overflow', 'token_overflow_quoted')
assert case in success_cases + wire_errors + token_errors

# Public tokenization must retain both its legacy global-token side effect and
# its EOF behavior after comments. These calls use the production tokenizer.
gdb.execute('call (void)Cmd_TokenizeString("probe last")')
assert gdb.parse_and_eval('com_token').string() == 'last'
for comment in ('// trailing', '/* trailing */'):
    gdb.execute('call (void)Cmd_TokenizeString("probe last %s")' % comment)
    assert int(gdb.parse_and_eval('Cmd_Argc()')) == 2
    assert gdb.parse_and_eval('Cmd_Argv(1)').string() == 'last'
    assert gdb.parse_and_eval('com_token').string() == ''
prefix = b'//fullserverinfo "'
head = b'\\metadata\\'
tail = b'\\sv_gravity\\600'
suffix = b'"\n'
if case == 'fit':
    info_length = 2047 - len(prefix) - len(suffix)
elif case == 'beyond2k':
    info_length = 4096
elif case == 'capacity':
    info_length = 8191
elif case == 'oversized':
    info_length = 8192
else:
    info_length = 200
info = head + b'x' * (info_length - len(head + tail)) + tail
command = prefix + info + suffix
assert len(info) == info_length
if case == 'comment_line':
    command = prefix + info + b'" // trailing comment'
elif case == 'comment_block':
    command = prefix + info + b'" /* trailing comment */  '
elif case in token_errors:
    third = b'x' * 8192
    if case == 'token_overflow_quoted':
        third = b'"' + third + b'"'
    command = prefix + info + b'" ' + third + b'\n'
elif case == 'ordinary_oversized':
    ordinary_prefix = b'//serverinfoupdate "metadata" "'
    command = ordinary_prefix + b'x' * (2048 - len(ordinary_prefix + suffix)) + suffix
    assert len(command) == 2048
if case == 'fit':
    assert len(command) == 2047
packet = bytes([9]) + command + (b'' if case == 'unterminated' else b'\0')
gdb.execute('set $packet = (byte *)Mem_Alloc(%d)' % len(packet))
gdb.selected_inferior().write_memory(int(gdb.parse_and_eval('$packet')), packet)
gdb.execute('set net_message.data = $packet')
gdb.execute('set net_message.cursize = %d' % len(packet))
gdb.execute('set net_message.maxsize = %d' % len(packet))
gdb.execute('call (void)Cmd_AddCommand2("fullserverinfo", CL_ServerExtension_FullServerinfo_f, src_server, 0)')
gdb.execute('call (void)Cmd_AddCommand2("serverinfoupdate", CL_ServerExtension_ServerinfoUpdate_f, src_server, 0)')

class Callback(gdb.Breakpoint):
    hits = 0
    def stop(self):
        self.hits += 1
        return False

callback = Callback('*CL_ServerExtension_FullServerinfo_f', internal=True)
update_callback = Callback('*CL_ServerExtension_ServerinfoUpdate_f', internal=True)
error = gdb.Breakpoint('Host_Error', internal=True)
try:
    if case in token_errors:
        # Skip svc_stufftext and the // marker: isolate token overflow from the
        # wire-string capacity guard, with a valid two-argument prefix in place.
        assert int(gdb.parse_and_eval('Cmd_ExecuteString((char *)$packet + 3, src_server)')) == 0, \
            'oversized server token was accepted'
        assert int(gdb.parse_and_eval('Cmd_Argc()')) == 2, 'overflowing token was added to argv'
    else:
        gdb.execute('call (void)CL_ParseServerMessage()')
except gdb.error:
    # GDB interrupts an inferior call when the actual Host_Error entry is hit.
    if case not in wire_errors or gdb.newest_frame().name() != 'Host_Error':
        raise
    assert 'truncated server command' in gdb.parse_and_eval('error').string()
    assert callback.hits == 0
    assert update_callback.hits == 0
else:
    assert case not in wire_errors, 'oversized/unterminated command was dispatched'
    assert update_callback.hits == 0
    if case in token_errors:
        assert callback.hits == 0, 'valid prefix executed despite trailing token overflow'
    else:
        assert callback.hits == 1, callback.hits
        actual = gdb.parse_and_eval('cl.serverinfo').string().encode()
        assert actual == info, (len(actual), len(info), actual[:20], info[:20], actual[-25:], info[-25:])
        assert int(gdb.parse_and_eval('PMCL_SetMoveVars()')) == 1
        assert float(gdb.parse_and_eval('movevars.gravity')) == 600.0
print('SERVERINFO_COMMAND_%s_PASSED' % case)
end
quit 0
