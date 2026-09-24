# Native QC runtime regression adapted from the QuakeSpasm OpenVR akimbo
# fixture. It drives the installed, hash-pinned QBJ3 program through the
# target's private usercmd and SV_Begin/EndPrivateVRWeaponPose scope.
# Run tests/vr_qbj3_akimbo_runtime.sh; no headset or XR runtime is used.
set pagination off
set confirm off
set debuginfod enabled off
set python print-stack full
break SV_Physics
run

set $client = &svs.clients[0]
set $p = EDICT_NUM(1)
set $client->active = 1
set $client->spawned = 1
set $client->knowntoqc = 1
set $client->edict = $p
set $client->protocol_qsvr = 1
if realtime <= 0
  set realtime = 1
end
set $client->lastmovetime = realtime
set $client->message.data = $client->msgbuf
set $client->message.maxsize = sizeof($client->msgbuf)
set $client->datagram.data = $client->datagram_buf
set $client->datagram.maxsize = sizeof($client->datagram_buf)

set $animcontroller = GetEdictFieldValueByName($p,"animcontroller")
set $controller = ED_Alloc()
set $controller->v.owner = (int)((char *)$p - (char *)qcvm->edicts)
set $animcontroller->edict = (int)((char *)$controller - (char *)qcvm->edicts)
set pr_global_struct->self = (int)((char *)$p - (char *)qcvm->edicts)
set $put = ED_FindFunction("PutClientInServer")
call PR_ExecuteProgram($put - qcvm->functions)

python
import gdb
vm = gdb.parse_and_eval('qcvm')
assert int(vm['progssize']) == 905470, 'loaded QBJ3 program has the wrong size'
loaded_hash = bytes(int(vm['progssha256'][i]) for i in range(32)).hex()
assert loaded_hash == 'de2c6a60df24f5ce0c3fc41b0fd6309105a0ea7ae895dfb4a6867950b9b90e34', \
    'target did not load the hash-pinned installed QBJ3 progs.dat: ' + loaded_hash
end

set $p->v.weaponmodel = PR_SetEngineString("progs/v_tnailgun.mdl")
set $p->v.weapon = 4
set $p->v.items = (int)$p->v.items | 4
set $p->v.ammo_nails = 50
set $p->v.view_ofs[0] = 0
set $p->v.view_ofs[1] = 0
set $p->v.view_ofs[2] = 22
set $p->v.v_angle[0] = 0
set $p->v.v_angle[1] = 0
set $p->v.v_angle[2] = 0

set $client->cmd.sequence = 1
set $client->cmd.msec = 50
set $client->cmd.seconds = 0.05
set $client->cmd.vr_active = 1
set $client->cmd.vr_handpos_relative = 1
set $client->cmd.vr_handpos[0] = 0
set $client->cmd.vr_handpos[1] = 0
set $client->cmd.vr_handpos[2] = 22
set $client->cmd.vr_handrot[0] = 0
set $client->cmd.vr_handrot[1] = 0
set $client->cmd.vr_handrot[2] = 0
set $client->cmd.vr_akimbo_active = 1
set $client->cmd.vr_akimbo_berserk = 0
set $client->cmd.vr_akimbo_muzzle[0][0] = 0
set $client->cmd.vr_akimbo_muzzle[0][1] = 8
set $client->cmd.vr_akimbo_muzzle[0][2] = 22
set $client->cmd.vr_akimbo_muzzle[1][0] = 0
set $client->cmd.vr_akimbo_muzzle[1][1] = -8
set $client->cmd.vr_akimbo_muzzle[1][2] = 22
set $client->cmd.vr_akimbo_angles[0][0] = 0
set $client->cmd.vr_akimbo_angles[0][1] = 90
set $client->cmd.vr_akimbo_angles[0][2] = 90
set $client->cmd.vr_akimbo_angles[1][0] = 0
set $client->cmd.vr_akimbo_angles[1][1] = 270
set $client->cmd.vr_akimbo_angles[1][2] = -90
set $client->cmd.vr_contact_received = realtime

set $attack = GetEdictFieldValueByName($p,"attackhold")
set $attack->_float = 1
set $pressed = GetEdictFieldValueByName($p,"attackpressed")
set $pressed->_float = 1
set $nextfire = GetEdictFieldValueByName($p,"nextthink1")
set $attack_finished = GetEdictFieldValueByName($p,"attack_finished")
set $fire = ED_FindFunction("weaponanim_twinnailgun_loop")
set $p->v.think = $fire - qcvm->functions
set sv_aim.value = 1
set $body = $p->v.origin

define fire_vr_phase
  set $p->v.weaponframe = $arg0 - 1
  set $nextfire->_float = qcvm->time + 1
  set $attack_finished->_float = qcvm->time
  set $p->v.think = $fire - qcvm->functions
  set $p->v.nextthink = qcvm->time
  set $client->lastmovetime = realtime
  set $client->cmd.vr_contact_received = realtime
  set pr_global_struct->self = (int)((char *)$p - (char *)qcvm->edicts)
  set pr_global_struct->time = qcvm->time
  set $ammo_before = $p->v.ammo_nails
  set $body_before = $p->v.origin
  set $angles_before = $p->v.v_angle
  set $forward_before = pr_global_struct->v_forward
  set $right_before = pr_global_struct->v_right
  set $up_before = pr_global_struct->v_up
  call SV_RunPrivateVRWeaponThink($p,$client)
  set $phase = $p->v.weaponframe
  python
import gdb, math
p = gdb.parse_and_eval('$p').dereference()
phase = int(gdb.parse_and_eval('$phase'))
assert phase in (11, 15), 'QC did not execute a twin-nail firing frame: %d' % phase
assert float(p['v']['ammo_nails']) == float(gdb.parse_and_eval('$ammo_before')) - 1, \
    'QC did not consume exactly one nail'
assert all(abs(float(p['v']['origin'][i]) - float(gdb.parse_and_eval('$body_before[%d]' % i))) < .001 for i in range(3)), \
    'target pose scope did not restore player origin'
assert all(abs(float(p['v']['v_angle'][i]) - float(gdb.parse_and_eval('$angles_before[%d]' % i))) < .001 for i in range(3)), \
    'target pose scope did not restore player angles'
for basis in ('forward', 'right', 'up'):
    for i in range(3):
        actual = float(gdb.parse_and_eval('pr_global_struct->v_%s[%d]' % (basis, i)))
        prior = float(gdb.parse_and_eval('$%s_before[%d]' % (basis, i)))
        assert abs(actual-prior) < .001, 'target pose scope did not restore %s basis' % basis

matches = []
for i in range(1, int(gdb.parse_and_eval('qcvm->num_edicts'))):
    e = gdb.parse_and_eval('EDICT_NUM(%d)' % i)
    if int(e['free']):
        continue
    classname = gdb.parse_and_eval('PR_GetString(%d)' % int(e['v']['classname'])).string()
    if classname == 'nail' and int(e['v']['owner']) == int(gdb.parse_and_eval('pr_global_struct->self')):
        matches.append(e)
assert matches, 'native QC did not spawn a player-owned nail'
nail = matches[-1]
origin = [float(nail['v']['origin'][i]) for i in range(3)]
velocity = [float(nail['v']['velocity'][i]) for i in range(3)]
hand = 1 if phase == 11 else 0
body = [float(gdb.parse_and_eval('$body[%d]' % i)) for i in range(3)]
muzzle = [body[i] + float(gdb.parse_and_eval('$client->cmd.vr_akimbo_muzzle[%d][%d]' % (hand, i))) for i in range(3)]
assert all(abs(origin[i]-muzzle[i]) < .02 for i in range(3)), \
    'projectile origin does not match selected physical muzzle: %r != %r' % (origin,muzzle)
expected_y = -1996.0 if phase == 11 else 1996.0
assert abs(velocity[0]) < .02 and abs(velocity[1]-expected_y) < .02 and abs(velocity[2]) < .02, \
    'projectile direction does not match anatomical hand pose: %r' % velocity
assert abs(math.sqrt(sum(v*v for v in velocity))-1996.0) < .02, 'unexpected projectile speed'
damage = float(gdb.parse_and_eval('GetEdictFieldValueByName(%s,"dmg")' % str(nail)).dereference()['_float'])
now = float(gdb.parse_and_eval('qcvm->time'))
assert int(damage) == 9, 'QC damage changed: %s' % damage
assert abs(float(gdb.parse_and_eval('$nextfire->_float'))-now-.08) < .001, 'QC firing cadence nextthink1 changed'
assert abs(float(gdb.parse_and_eval('$attack_finished->_float'))-now-.09) < .001, 'QC attack_finished cadence changed'
gdb.execute('set $nail = EDICT_NUM(%d)' % int(gdb.parse_and_eval('NUM_FOR_EDICT(%s)' % str(nail))))
gdb.write('QBJ3_AKIMBO_VR_PHASE_PASS frame=%d ammo=%s muzzle=%s direction=%s damage=%d cadence=0.08/0.09 restore=pass\n' %
          (phase, p['v']['ammo_nails'], origin, velocity, int(damage)))
end
  call ED_Free($nail)
end

fire_vr_phase 11
fire_vr_phase 15

# An ordinary client has no private protocol or VR command. The same installed
# QC must retain its stock muzzle, aim and firing behavior through the scope.
set $client->protocol_qsvr = 0
set $client->lastmovetime = 0
set $client->cmd.vr_active = 0
set $client->cmd.vr_handpos_relative = 0
set $client->cmd.vr_akimbo_active = 0
set $p->v.weaponframe = 10
set $p->v.think = $fire - qcvm->functions
set $p->v.nextthink = qcvm->time
set $nextfire->_float = qcvm->time + 1
set $attack_finished->_float = qcvm->time
set $p->v.v_angle[0] = 0
set $p->v.v_angle[1] = 0
set $p->v.v_angle[2] = 0
set pr_global_struct->v_forward[0] = 1
set pr_global_struct->v_forward[1] = 0
set pr_global_struct->v_forward[2] = 0
set pr_global_struct->v_right[0] = 0
set pr_global_struct->v_right[1] = -1
set pr_global_struct->v_right[2] = 0
set pr_global_struct->v_up[0] = 0
set pr_global_struct->v_up[1] = 0
set pr_global_struct->v_up[2] = 1
set $ammo_before = $p->v.ammo_nails
set $body_before = $p->v.origin
call SV_RunPrivateVRWeaponThink($p,$client)
python
import gdb
p = gdb.parse_and_eval('$p').dereference()
assert float(p['v']['ammo_nails']) == float(gdb.parse_and_eval('$ammo_before')) - 1, \
    'ordinary QC fallback did not consume one nail'
matches=[]
for i in range(1,int(gdb.parse_and_eval('qcvm->num_edicts'))):
    e=gdb.parse_and_eval('EDICT_NUM(%d)'%i)
    if int(e['free']): continue
    name=gdb.parse_and_eval('PR_GetString(%d)'%int(e['v']['classname'])).string()
    if name=='nail' and int(e['v']['owner'])==int(gdb.parse_and_eval('pr_global_struct->self')): matches.append(e)
assert matches, 'ordinary QC fallback spawned no nail'
nail=matches[-1]
origin=[float(nail['v']['origin'][i]) for i in range(3)]
body=[float(gdb.parse_and_eval('$body_before[%d]'%i)) for i in range(3)]
expected=[body[0]+11,body[1]-4,body[2]+16]
assert all(abs(origin[i]-expected[i])<.02 for i in range(3)), \
    'ordinary stock projectile origin changed: %r != %r'%(origin,expected)
velocity=[float(nail['v']['velocity'][i]) for i in range(3)]
assert abs(velocity[0]-1996)<.02 and abs(velocity[1])<.02 and abs(velocity[2])<.02, \
    'ordinary stock projectile direction changed: %r'%velocity
assert all(abs(float(p['v']['origin'][i])-body[i])<.001 for i in range(3)), \
    'ordinary scope modified player origin'
gdb.write('QBJ3_AKIMBO_ORDINARY_FALLBACK_PASS origin=%s direction=%s ammo=%s\n'%
          (origin,velocity,p['v']['ammo_nails']))
gdb.execute('set $ordinary_nail = EDICT_NUM(%d)'%int(gdb.parse_and_eval('NUM_FOR_EDICT(%s)'%str(nail))))
end
call ED_Free($ordinary_nail)
printf "QBJ3_AKIMBO_RUNTIME_PASS installed QC, both VR hands, cadence/damage, scope restoration, ordinary fallback\n"
quit 0
