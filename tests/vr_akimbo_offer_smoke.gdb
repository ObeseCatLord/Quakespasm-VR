# Exercise real stufftext dispatch and server-only akimbo policy without assets.
# gdb -nx --batch -x tests/vr_akimbo_offer_smoke.gdb --args Quake/vkquake -novr
set pagination off
set confirm off
set debuginfod enabled off
set python print-stack full
break main
run -novr
python
import gdb

gdb.execute('call (void)Cmd_AddCommand2("vr_qbj3_akimbo_protocol", CL_ServerExtension_AkimboProtocol_f, src_server, 0)')

def value(expr): return int(gdb.parse_and_eval(expr))
def flags():
    return tuple(value('cl.' + field) for field in (
        'vr_qbj3_akimbo_supported', 'vr_qbj3_berserk_akimbo_supported',
        'vr_enyo_akimbo_supported', 'vr_dwell_berserk_akimbo_supported'))
def offer(args, source='src_server'):
    command = 'vr_qbj3_akimbo_protocol ' + args
    return value('Cmd_ExecuteString(' + repr(command).replace("'", '"') + ', ' + source + ')')
def wire_offer(args):
    packet = bytes([9]) + ('//vr_qbj3_akimbo_protocol ' + args + '\n').encode() + b'\0'
    gdb.execute('set $packet = (byte *)Mem_Alloc(%d)' % len(packet))
    gdb.selected_inferior().write_memory(value('$packet'), packet)
    gdb.execute('set net_message.data = $packet')
    gdb.execute('set net_message.cursize = %d' % len(packet))
    gdb.execute('set net_message.maxsize = %d' % len(packet))
    gdb.execute('call (void)CL_ParseServerMessage()')

assert flags() == (0, 0, 0, 0)
wire_offer('1 1 1 1')
assert flags() == (0, 0, 0, 0), 'public protocol accepted akimbo offer'
gdb.execute('set cl.protocol_qsvr = 1')  # QSVR_PROTOCOL_PINNED
wire_offer('1 0 1 0')
assert flags() == (1, 0, 1, 0), 'pinned stuffed offer not applied'
offer('0 0 0 0', 'src_command')
assert flags() == (1, 0, 1, 0), 'local command changed server policy'
wire_offer('1 1 "1')
assert flags() == (0, 0, 0, 0), 'malformed offer left permission active'
wire_offer('0 1 0 1')
assert flags() == (0, 1, 0, 1)
wire_offer('1 1 1 1 junk')
assert flags() == (0, 0, 0, 0), 'trailing argument left permission active'
wire_offer('1 1 1 1')
assert flags() == (1, 1, 1, 1)
gdb.execute('call (void)CL_FreeState()')
assert flags() == (0, 0, 0, 0), 'disconnect did not clear support'
print('VR_AKIMBO_OFFER_PASSED')
end
quit 0
