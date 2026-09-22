# Initialized production parser; caller supplies an isolated asset profile.
# QSVR_HEADER_CASE selects a literal serverinfo header. A valid tuple deliberately
# ends with maxclients=0, proving admission without attempting model/world loading.
set pagination off
set confirm off
set debuginfod enabled off
break Host_Frame
run
python
import os, struct, gdb
case=os.environ['QSVR_HEADER_CASE']
assert case in ('valid','missing_bit','extra_bit','nonzero_pext1','wrong_flags','more_flags','truncated','no_extensions','public_private_header')
def execute(s): gdb.execute(s,to_string=True)
def integer(s): return int(gdb.parse_and_eval(s))
pack=lambda n: struct.pack('<I',n)
pext2=0xe9
flags=0x12
if case=='missing_bit': pext2 &= ~1
if case=='extra_bit': pext2 |= 2
if case=='wrong_flags': flags=0x10
if case=='more_flags': flags |= 0x80000000
header=b''
if case=='nonzero_pext1': header += b'FTEX'+pack(0x40000000)
if case!='no_extensions': header += b'FTE2'+pack(pext2)
header += pack(999)+pack(flags)
if case=='truncated': header=header[:-1]
else: header+=b'\0\0' # empty gamedir, invalid maxclients sentinel
packet=bytes([11])+header
execute('set cls.legacy_qsvr = %d' % (0 if case=='public_private_header' else 1))
execute('set cls.demoplayback = 0')
execute('set $packet = (byte *)Mem_Alloc(%d)' % len(packet))
gdb.selected_inferior().write_memory(integer('$packet'),packet)
execute('set net_message.data = $packet')
execute('set net_message.cursize = %d' % len(packet))
execute('set net_message.maxsize = %d' % len(packet))
error=gdb.Breakpoint('Host_Error',internal=True)
try:
    execute('call (void)CL_ParseServerMessage()')
except gdb.error:
    assert gdb.newest_frame().name()=='Host_Error'
    message=gdb.parse_and_eval('error').string()
    if case=='valid':
        assert integer('cl.protocol_qsvr')==1
        assert message.startswith('Bad maxclients'),message
    else:
        assert integer('cl.protocol_qsvr')==0,'malformed header admitted private dialect'
        assert not message.startswith('Bad maxclients'),message
else:
    raise AssertionError('malformed/sentinel header did not fail')
print('PRIVATE_HEADER_%s_PASSED' % case.upper())
end
quit
