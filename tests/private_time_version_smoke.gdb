# Actual complete-message parser checks in the Linux debug executable.
# Injects exact svc_time/svc_version wire bytes before engine initialization.
# QSVR_TIME_VERSION_CASE: public_predinfo_time, public_plain_time,
# private_time, private_nonrmq_version, private_rmq_version,
# public_netquake_version, public_fitz_version.
set pagination off
set confirm off
set debuginfod enabled off
set python print-stack full
break main
run -novr
python
import os
import struct
import gdb

case = os.environ['QSVR_TIME_VERSION_CASE']
cases = (
    'public_predinfo_time',
    'public_plain_time',
    'private_time',
    'private_nonrmq_version',
    'private_rmq_version',
    'public_netquake_version',
    'public_fitz_version',
)
assert case in cases

SVC_UPDATESTAT = 3
SVC_VERSION = 4
SVC_TIME = 7
PROTOCOL_NETQUAKE = 15
PROTOCOL_FITZQUAKE = 666
PROTOCOL_RMQ = 999
PEXT2_PREDINFO = 0x20
QSVR_PROTOCOL_PINNED = 1
QSVR_PEXT2_REQUIRED = 0xe9
STAT_HEALTH = 0
STAT_SENTINEL = 0x12345678

def execute(command):
    gdb.execute(command, to_string=True)

def integer(expression):
    return int(gdb.parse_and_eval(expression))

execute('call (void)memset(&cl, 0, sizeof(cl))')
execute('call (void)memset(&cls, 0, sizeof(cls))')
execute('set $packet = (byte *)Mem_Alloc(64)')

private = case.startswith('private_')
execute('set cl.protocol_qsvr = %d' % (QSVR_PROTOCOL_PINNED if private else 0))
execute('set cl.protocol_pext2 = %d' % (QSVR_PEXT2_REQUIRED if private else 0))
execute('set cl.protocol = %d' % PROTOCOL_RMQ)

def adjacent_stat():
    return bytes([SVC_UPDATESTAT, STAT_HEALTH]) + struct.pack('<i', STAT_SENTINEL)

if case == 'public_predinfo_time':
    execute('set cl.protocol_pext2 = %d' % PEXT2_PREDINFO)
    # Low sequence byte is an invalid service if the parser fails to consume it.
    packet = bytes([SVC_TIME]) + struct.pack('<fH', 12.5, 0x347f) + adjacent_stat()
elif case in ('public_plain_time', 'private_time'):
    packet = bytes([SVC_TIME]) + struct.pack('<f', 12.5) + adjacent_stat()
else:
    versions = {
        'private_nonrmq_version': PROTOCOL_NETQUAKE,
        'private_rmq_version': PROTOCOL_RMQ,
        'public_netquake_version': PROTOCOL_NETQUAKE,
        'public_fitz_version': PROTOCOL_FITZQUAKE,
    }
    packet = bytes([SVC_VERSION]) + struct.pack('<i', versions[case]) + adjacent_stat()

gdb.selected_inferior().write_memory(integer('$packet'), packet)
execute('set net_message.data = $packet')
execute('set net_message.cursize = %d' % len(packet))
execute('set net_message.maxsize = %d' % len(packet))

error = gdb.Breakpoint('Host_Error', internal=True)
try:
    execute('call (void)CL_ParseServerMessage()')
except gdb.error:
    if case != 'private_nonrmq_version' or gdb.newest_frame().name() != 'Host_Error':
        raise
    assert 'requires RMQ throughout the connection' in gdb.parse_and_eval('error').string()
    assert integer('cl.protocol') == PROTOCOL_RMQ, 'private rejection changed cl.protocol'
else:
    assert case != 'private_nonrmq_version', 'private non-RMQ svc_version was accepted'
    assert integer('cl.stats[0]') == STAT_SENTINEL, 'adjacent svc_updatestat was not framed'
    if case.endswith('_time'):
        assert abs(float(gdb.parse_and_eval('cl.mtime[0]')) - 12.5) < 0.0001
    elif case == 'private_rmq_version':
        assert integer('cl.protocol') == PROTOCOL_RMQ
    elif case == 'public_netquake_version':
        assert integer('cl.protocol') == PROTOCOL_NETQUAKE
    elif case == 'public_fitz_version':
        assert integer('cl.protocol') == PROTOCOL_FITZQUAKE

print('PRIVATE_TIME_VERSION_%s_PASSED' % case.upper())
end
quit 0
