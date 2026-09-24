# Exercise the production server-command registry and effective client policy
# before engine initialization. No game assets or OpenXR runtime are needed.
# gdb -nx --batch -x tests/vr_weapon_contact_policy_smoke.gdb --args build-debug/vkquake -novr
set pagination off
set confirm off
set debuginfod enabled off
set python print-stack full
break main
run -novr
python
import gdb

gdb.execute('call (void)Cmd_AddCommand2("vr_weapon_contact_protocol", CL_ServerExtension_WeaponContactProtocol_f, src_server, 0)')

def value(expr): return int(gdb.parse_and_eval(expr))
def offer(args, source='src_server'):
    command = 'vr_weapon_contact_protocol ' + args
    return value('Cmd_ExecuteString(' + repr(command).replace("'", '"') + ', ' + source + ')')
def wire_offer(args):
    packet = bytes([9]) + ('//vr_weapon_contact_protocol ' + args + '\n').encode() + b'\0'
    gdb.execute('set $packet = (byte *)Mem_Alloc(%d)' % len(packet))
    gdb.selected_inferior().write_memory(value('$packet'), packet)
    gdb.execute('set net_message.data = $packet')
    gdb.execute('set net_message.cursize = %d' % len(packet))
    gdb.execute('set net_message.maxsize = %d' % len(packet))
    gdb.execute('call (void)CL_ParseServerMessage()')
def mode(): return value('cl.vr_weapon_contact_mode')
def profile(): return value('cl.vr_weapon_contact_profile')
def authorized(): return value('VR_WeaponCollisionAuthorized()')

gdb.execute('set vr_weapon_collision.value = 1')
assert mode() == 0 and not authorized()
offer('1 1 0', 'src_command')
assert mode() == 0, 'a local command changed server policy'

wire_offer('1 1 0')
assert mode() == 1 and profile() == 0
assert not authorized(), 'public peer enabled collision without private admission'
gdb.execute('set cl.protocol_qsvr = 1')  # QSVR_PROTOCOL_PINNED
assert authorized()

assert offer('1 2 1')
assert mode() == 2 and profile() == 1 and not authorized(), 'melee-only bit enabled collision'
assert offer('1 3 1')
assert mode() == 3 and authorized()
assert offer('1 4 0')
assert mode() == 0 and profile() == 0 and not authorized(), 'unknown mode did not revoke'

assert offer('1 1 0') and authorized()
assert offer('1 1 0 junk')
assert mode() == 0 and not authorized(), 'trailing argument did not revoke'
wire_offer('1 1 0')
assert authorized()
wire_offer('1 1 "0')
assert mode() == 0 and not authorized(), 'unterminated quote authorized collision'
wire_offer('1 1 0')
assert authorized()
wire_offer('1 1 0 /*')
assert mode() == 0 and not authorized(), 'unterminated comment authorized collision'
assert offer('2 1 0')
assert mode() == 0 and not authorized(), 'unsupported version did not revoke'
assert offer('1 1 0')
gdb.execute('set vr_weapon_collision.value = 0')
assert not authorized(), 'local opt-out ignored'
gdb.execute('call (void)CL_ResetWeaponContactState()')
assert mode() == 0 and profile() == 0
print('VR_WEAPON_CONTACT_POLICY_PASSED')
end
quit 0
