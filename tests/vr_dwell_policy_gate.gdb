# Check current policy and exact installed Dwell program recognition.
# Offer serialization is reviewed at the two callers of this policy helper.
set pagination off
set confirm off
set debuginfod enabled off
set print thread-events off
break SV_Physics
run
python
import gdb

def value(expr):
    return int(gdb.parse_and_eval(expr))

def policy(text):
    gdb.execute('call (void)Cvar_SetQuick(&sv_immersive_melee, "%s")' % text,
                to_string=True)

assert value('SV_DwellBerserkAkimboProgramLoaded()'), 'wrong Dwell program'
assert not value('SV_VRDwellBerserkMeleeEnabled()'), 'melee default changed'
policy('1')
assert value('SV_VRDwellBerserkMeleeEnabled()'), 'explicit enable rejected'
policy('0')
assert not value('SV_VRDwellBerserkMeleeEnabled()'), 'disable retained permission'
policy('1')
gdb.execute('set qcvm->progssha256[0] = 0')
assert not value('SV_DwellBerserkAkimboProgramLoaded()')
assert not value('SV_VRDwellBerserkMeleeEnabled()'), 'modified program admitted'
print('VR_DWELL_POLICY_GATE_PASSED')
end
quit 0
