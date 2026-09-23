# Actual complete-message parser checks in the Linux debug executable.
# Injects wire bytes before engine initialization; no sockets, replay or map.
set pagination off
set confirm off
set debuginfod enabled off
set python print-stack full
break main
run -novr
python
import gdb
import struct

def execute(command):
    gdb.execute(command, to_string=True)

def integer(expression):
    return int(gdb.parse_and_eval(expression))

execute('set $entities = (entity_t *)Mem_Alloc(4 * sizeof(entity_t))')
execute('set $packet = (byte *)Mem_Alloc(8192)')

def reset():
    execute('call (void)memset(&cl, 0, sizeof(cl))')
    execute('call (void)memset($entities, 0, 4 * sizeof(entity_t))')
    for key, value in {'protocol_qsvr': 1, 'protocol_pext2': 0xe9,
                       'movemessages': 100, 'ackedmovemessages': -1,
                       'viewentity': 1, 'max_edicts': 4,
                       'num_entities': 2, 'maxclients': 1}.items():
        execute('set cl.%s = %d' % (key, value))
    execute('set cl.entities = $entities')
    execute('set cl.entities[1].baseline.origin[0] = 12')
    execute('set net_message.data = $packet')
    execute('set net_message.maxsize = 8192')

def ack(sequence=10, epoch=1):
    # accepted authority, prediction allowed, engine PMove, mode/discontinuity
    return struct.pack('<HBBHHB', sequence, 3, 2, epoch, 1, 0)

def snapshot(sequence=10, epoch=1, owner=True):
    # svcfte_updateentities, pinned ACK, time, owner + UF_RESET, terminator.
    entities = struct.pack('<HBB', 1, 0x80, 0x01) if owner else b''
    return bytes([86]) + ack(sequence, epoch) + struct.pack('<f', 3.0) + entities + b'\0\0'

def movement_stats():
    # Complete movement settings in this one message, including zero values.
    packet = struct.pack('<BBI', 3, 225, 0x80000000)
    for stat in list(range(226, 230)) + list(range(238, 240)) + list(range(241, 254)):
        packet += struct.pack('<BBf', 79, stat, 0.0)
    return packet

def parse(packet, include_stats=True):
    if include_stats:
        packet = movement_stats() + packet
    assert len(packet) <= 8192
    gdb.selected_inferior().write_memory(integer('$packet'), packet)
    execute('set net_message.cursize = %d' % len(packet))
    execute('call (void)CL_ParseServerMessage()')

def valid(expected):
    assert bool(integer('cl.move_snapshot_valid')) == expected

reset()
parse(snapshot(), include_stats=False)
valid(False)

reset()
parse(snapshot())
valid(True)
assert integer('cl.move_snapshot_owner') == 1
assert integer('cl.move_snapshot_ack') == 10
assert float(gdb.parse_and_eval('cl.entities[1].netstate.origin[0]')) == 12

# A later owner reset without its own complete stat group cannot keep or
# re-establish eligibility from the preceding datagram.
parse(snapshot(), include_stats=False)
valid(False)

# A later accepted standalone ACK in the SAME message breaks the association.
reset()
parse(snapshot() + bytes([57]) + ack(epoch=2))
valid(False)

# An ignored stale standalone ACK cannot replace accepted candidate metadata.
reset()
parse(snapshot() + bytes([57]) + ack(sequence=9, epoch=2))
valid(True)
assert integer('cl.move_ack_mode_epoch') == 1

# The real svc_setview owner invalidates even a change away and back.
reset()
parse(snapshot() + bytes([5, 2, 0, 5, 1, 0]))
valid(False)

# A later replacement service omitting the owner cannot reuse an earlier one.
reset()
parse(snapshot() + snapshot(owner=False))
valid(False)

# Candidate selection must wait for the WHOLE message, including later services.
reset()
error = gdb.Breakpoint('Host_Error', internal=True)
try:
    parse(snapshot() + bytes([57]) + ack()[:-1])
except gdb.error:
    assert gdb.newest_frame().name() == 'Host_Error'
    assert 'Bad server message' in gdb.parse_and_eval('error').string()
    valid(False)
else:
    raise AssertionError('truncated trailing ACK escaped the message error')
print('PRIVATE_OWNER_MESSAGE_BOUNDARY_PASSED')
end
quit 0
