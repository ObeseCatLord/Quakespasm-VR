set pagination off
set confirm off
set debuginfod enabled off
set python print-stack full
break SV_Physics
run
python
import gdb, math, json

def ev(s): return gdb.parse_and_eval(s)
def cmd(s): return gdb.execute(s, to_string=True)
def setv(s,v): cmd('set '+s+' = '+str(v))
def vec(s): return [float(ev(s+'[%d]'%i)) for i in range(3)]
def setvec(s,v):
    for i,x in enumerate(v): setv(s+'[%d]'%i,x)
def near(a,b,tol=.02): return all(abs(x-y)<tol for x,y in zip(a,b))
def field(e,name): return 'GetEdictFieldValueByName(%s,%s)->_float'%(e,json.dumps(name))
def glob(name): return 'qcvm->globals[ED_FindGlobal(%s)->ofs]'%json.dumps(name)

def owned_nails():
    found=[]
    for i in range(1,int(ev('qcvm->num_edicts'))):
        e='((edict_t *)((char *)qcvm->edicts + %d * qcvm->edict_size))'%i
        if int(ev(e+'->free')): continue
        if int(ev(e+'->v.owner'))==int(ev('pr_global_struct->self')) and ev('PR_GetString('+e+'->v.classname)').string()=='proj_nail': found.append(e)
    return found

setv('$client','&svs.clients[0]'); setv('$p','EDICT_NUM(1)')
for n,v in [('active',1),('spawned',1),('knowntoqc',1),('edict','$p'),('protocol_qsvr',1),('lastmovetime','realtime'),('message.data','$client->msgbuf'),('message.maxsize','sizeof($client->msgbuf)'),('datagram.data','$client->datagram_buf'),('datagram.maxsize','sizeof($client->datagram_buf)')]: setv('$client->'+n,v)
if float(ev('realtime')) <= 0: setv('realtime',1)
setv('$client->lastmovetime','realtime')
assert int(ev('SV_PerilAkimboProgramLoaded()'))==1
setv('qcvm->progssha256[0]',0); assert int(ev('SV_PerilAkimboProgramLoaded()'))==0; setv('qcvm->progssha256[0]',0x5e)
assert int(ev('SV_QBJ3TwinNailgunProgramLoaded()'))==0, 'Peril must not enable QBJ3 melee identity'
print('PERIL_GATE_PASS exact native QC, hash rejection, QBJ3 identity isolation')
setv('pr_global_struct->self','EDICT_TO_PROG($p)')
cmd('call PR_ExecuteProgram(ED_FindFunction("PutClientInServer") - qcvm->functions)')
setv('$p->v.weaponmodel','PR_SetEngineString("progs/v_nail.mdl")');setv('$p->v.weapon',4);setv('$p->v.ammo_nails',100)
setv('$p->v.items','(int)$p->v.items | 4');setv('$p->v.button0',1)
setvec('$p->v.v_angle',[0,0,0]);setvec('$p->v.view_ofs',[0,0,22])
for n,v in [('sequence',1),('msec',50),('seconds',.05),('vr_active',1),('vr_handpos_relative',1),('vr_akimbo_active',1),('vr_akimbo_berserk',0),('vr_contact_received','realtime')]:setv('$client->cmd.'+n,v)
setvec('$client->cmd.vr_handpos',[0,0,22]);setvec('$client->cmd.vr_handrot',[0,0,0])
setvec('$client->cmd.vr_akimbo_muzzle[0]',[0,8,22]);setvec('$client->cmd.vr_akimbo_muzzle[1]',[0,-8,22])
setvec('$client->cmd.vr_akimbo_angles[0]',[0,90,90]);setvec('$client->cmd.vr_akimbo_angles[1]',[0,270,-90]);setv('sv_aim.value',1)
for n in ['intermission_running','cinematic_running']:setv(glob(n),0)
body=vec('$p->v.origin');print('PERIL_BODY',body)

def fire(frame,auto,mode='vr',refresh=True,weapon='nail'):
    setv(glob('autoaim_cvar'),auto)
    setv('$p->v.think','ED_FindFunction("player_%s%d") - qcvm->functions'%(weapon,frame))
    setv('$p->v.nextthink','qcvm->time');setv('pr_global_struct->self','EDICT_TO_PROG($p)');setv('pr_global_struct->time','qcvm->time')
    if refresh:setv('$client->lastmovetime','realtime');setv('$client->cmd.vr_contact_received','realtime')
    ammo=float(ev('$p->v.ammo_nails')); prior=vec('$p->v.origin'); angles=vec('$p->v.v_angle');basis=[vec('pr_global_struct->v_'+n) for n in ['forward','right','up']]
    cmd('call SV_RunPrivateVRWeaponThink($p,$client,&$client->cmd)')
    assert int(ev('$p->v.weaponframe'))==frame
    assert float(ev('$p->v.ammo_nails'))==ammo-(2 if weapon=='snail' else 1)
    assert near(vec('$p->v.origin'),prior,.001), 'origin not restored'
    assert near(vec('$p->v.v_angle'),angles,.001), 'angles not restored'
    for n,b in zip(['forward','right','up'],basis): assert near(vec('pr_global_struct->v_'+n),b,.001),'basis not restored'
    assert abs(float(ev(field('$p','attack_finished')))-float(ev('qcvm->time'))-.2)<.001
    assert abs(float(ev('$p->v.nextthink'))-float(ev('qcvm->time'))-.1)<.001
    found=owned_nails()
    assert len(found)==1,'expected exactly one native projectile, got '+str(found)
    e=found[0]; origin=vec(e+'->v.origin'); velocity=vec(e+'->v.velocity')
    assert float(ev(field(e,'classtype')))==(4210 if weapon=='snail' else 4200), 'native projectile type changed'
    assert abs(math.sqrt(sum(x*x for x in velocity))-1000)<.02
    if mode=='vr':
        hand=frame&1; expected=[prior[i]+float(ev('$client->cmd.vr_akimbo_muzzle[%d][%d]'%(hand,i))) for i in range(3)]
        assert near(origin,expected), 'physical muzzle mismatch '+str((origin,expected))
        assert near(velocity,[0,-1000 if hand else 1000,0]), 'physical direction mismatch '+str(velocity)
    elif mode=='clamped':
        assert near(origin,expected_clamp), 'BSP clamp mismatch '+str((origin,expected_clamp))
        assert near(velocity,[0,-1000,0])
    elif mode=='target':
        expected=[0,-64000/math.sqrt(64*64+8*8),8000/math.sqrt(64*64+8*8)]
        assert near(velocity,expected), 'physical-muzzle target correction mismatch '+str((velocity,expected))
    elif mode=='desktop':
        ox=2 if frame&1 else -2
        if weapon=='snail': ox=[2.5,-3.5,2.5,0,-2.5,3.5,-2.5,0][frame-1]
        expected=[prior[0],prior[1]-ox,prior[2]+16]
        assert near(origin,expected),'desktop muzzle changed '+str((origin,expected))
        assert near(velocity,[1000,0,0]), 'desktop direction changed '+str(velocity)
    else:
        # Rejected paired commands retain the existing dominant-hand fallback:
        # generic source is dominant muzzle - 8forward - world16; native QC
        # then adds world16 + right*ox. The configured zero-roll/yaw dominant
        # muzzle equals the saved body eye, so no clamp adjustment is needed.
        ox=2 if frame&1 else -2
        if weapon=='snail': ox=[2.5,-3.5,2.5,0,-2.5,3.5,-2.5,0][frame-1]
        expected=[prior[0]-8,prior[1]-ox,prior[2]+22]
        assert near(origin,expected), 'rejected pair fallback origin mismatch '+str((origin,expected))
        assert near(velocity,[1000,0,0]), 'rejected pair retained hand direction '+str(velocity)
    cmd('call ED_Free('+e+')')
    print('PERIL_SHOT_PASS frame=%d auto=%s mode=%s weapon=%s origin=%s velocity=%s native type/ammo/cadence/restore=pass'%(frame,auto,mode,weapon,origin,velocity))
    return origin,velocity
# Native attack-input lifecycle. Initialize only once; QC selects the first
# firing callback and every subsequent STATE/think callback itself. No firing
# frame, think or nextthink is injected while the press/hold/release loop runs.
setv('$input_scope','(sv_vr_weapon_pose_scope_t *)calloc(1,sizeof(sv_vr_weapon_pose_scope_t))')
base=float(ev('qcvm->time'))+1
setv('qcvm->time',base);setv('pr_global_struct->time',base);setv('host_frametime',.025)
setv('$p->v.ammo_nails',7);setv('$p->v.ammo_shells',0);setv('$p->v.ammo_rockets',0);setv('$p->v.ammo_cells',0)
setv('$p->v.items',4096|4);setv('$p->v.impulse',0);setv('$p->v.button0',0);setvec('$p->v.velocity',[0,0,0])
setv('*(int *)&qcvm->globals[4]','EDICT_TO_PROG($p)')
cmd('call PR_ExecuteProgram(ED_FindFunction("W_SetCurrentAmmo") - qcvm->functions)')
setv(field('$p','attack_finished'),base-.1);setv(glob('autoaim_cvar'),1)
assert int(ev('$p->v.weaponframe'))==0 and float(ev('$p->v.currentammo'))==7
shots=[]; exhausted=False
for tick in range(49):
    now=base+tick*.025
    pressed=tick<15 or 24<=tick<40
    setv('qcvm->time',now);setv('pr_global_struct->time',now)
    setv('$client->cmd.buttons',int(pressed));setv('$p->v.button0','(int)$client->cmd.buttons & 1')
    setv('$client->lastmovetime','realtime');setv('$client->cmd.vr_contact_received','realtime')
    origin_before=vec('$p->v.origin');angles_before=vec('$p->v.v_angle')
    basis_before=[vec('pr_global_struct->v_'+n) for n in ['forward','right','up']]
    setv('pr_global_struct->self','EDICT_TO_PROG($p)')
    cmd('call SV_RunPrivateVRWeaponThink($p,$client,&$client->cmd)')
    # This is the production pose boundary used around QC PlayerPostThink.
    # Its native weapon/input entrypoint owns attack_finished and W_Attack.
    setv('pr_global_struct->time',now)
    cmd('call SV_BeginPrivateVRWeaponPose($p,$client,&$client->cmd,$input_scope)')
    cmd('call PR_ExecuteProgram(ED_FindFunction("W_WeaponFrame") - qcvm->functions)')
    cmd('call SV_EndPrivateVRWeaponPoseGuarded($p,$input_scope,1)')
    assert near(vec('$p->v.origin'),origin_before,.001), 'input lifecycle leaked player origin'
    assert near(vec('$p->v.v_angle'),angles_before,.001), 'input lifecycle leaked player angles'
    for n,b in zip(['forward','right','up'],basis_before): assert near(vec('pr_global_struct->v_'+n),b,.001), 'input lifecycle leaked '+n
    nails=owned_nails();assert len(nails)<=1, 'scheduled think/input duplicated a shot'
    if nails:
        assert pressed, 'release emitted a nail'
        frame=int(ev('$p->v.weaponframe'));hand=frame&1
        birth=float(ev(field('$p','attack_finished')))-.2
        origin=vec(nails[0]+'->v.origin');velocity=vec(nails[0]+'->v.velocity')
        expected=[body[i]+float(ev('$client->cmd.vr_akimbo_muzzle[%d][%d]'%(hand,i))) for i in range(3)]
        assert near(origin,expected), 'native input first/scheduled muzzle mismatch '+str((origin,expected))
        assert near(velocity,[0,-1000 if hand else 1000,0]), 'native input hand direction mismatch'
        assert float(ev(field(nails[0],'classtype')))==4200
        shots.append((birth,frame,hand));cmd('call ED_Free('+nails[0]+')')
        assert float(ev('$p->v.ammo_nails'))==7-len(shots), 'native input ammo/count mismatch'
    if 16<=tick<24: assert len(shots)==4, 'release failed to stop scheduled fire'
    if tick>=37:
        assert float(ev('$p->v.ammo_nails'))==0 and int(ev('$p->v.weapon'))!=4, 'empty-ammo QC did not switch weapon'
        exhausted=True
assert exhausted and len(shots)==7, 'press/hold/release/exhaustion shot count mismatch '+str(shots)
assert [frame for _,frame,_ in shots]==[1,2,3,4,1,2,3], 'QC callback sequence was restarted or skipped '+str(shots)
assert [hand for _,_,hand in shots]==[1,0,1,0,1,0,1]
for i,expected_time in enumerate([0,.1,.2,.3,.6,.7,.8]):
    assert abs(shots[i][0]-base-expected_time)<.002, 'native input cadence changed '+str(shots)
print('PERIL_INPUT_LIFECYCLE_PASS native W_WeaponFrame press/hold/release/repress/exhaustion; count=7 frames=1,2,3,4/1,2,3 hands=R,L,R,L/R,L,R cadence=0.1 first/scheduled physical muzzles and body/basis restore=pass')
cmd('call (void)free($input_scope)')
# Reset the scenario once for the existing exhaustive per-callback matrix.
setv('$p->v.weapon',4);setv('$p->v.weaponmodel','PR_SetEngineString("progs/v_nail.mdl")')
setv('$p->v.items',4096|4);setv('$p->v.ammo_nails',100);setv('$p->v.currentammo',100);setv('$p->v.button0',1)

for auto in [0,1]:
    for frame in range(1,9):fire(frame,auto)
# Public/desktop protocol fallback executes the same native callbacks.
setv('$client->protocol_qsvr',0);setv('$client->cmd.vr_active',0);setv('$client->cmd.vr_akimbo_active',0)
for auto in [0,1]:
    for frame in range(1,9):fire(frame,auto,'desktop')
# SNG is a single model and preserves its native rotating barrel offsets.
setv('$p->v.weapon',8);setv('$p->v.weaponmodel','PR_SetEngineString("progs/v_nail2.mdl")')
for frame in range(1,9):fire(frame,1,'desktop',weapon='snail')
# Valid private command must still not apply pair hooks to the SNG.
setv('$client->protocol_qsvr',1);setv('$client->cmd.vr_active',1);setv('$client->cmd.vr_akimbo_active',1)
fire(1,1,'rejected',weapon='snail')
setv('$p->v.weapon',4);setv('$p->v.weaponmodel','PR_SetEngineString("progs/v_nail.mdl")')
setv('qcvm->progssha256[0]',0);fire(1,1,'rejected');setv('qcvm->progssha256[0]',0x5e)
setv('$client->cmd.vr_contact_received','realtime - 0.3');fire(1,1,'rejected',refresh=False)
setv('$client->cmd.vr_akimbo_muzzle[0][0]',97);fire(1,1,'rejected');setv('$client->cmd.vr_akimbo_muzzle[0][0]',0)
print('PERIL_NATIVE_RUNTIME_PASS 16 VR shots,16 desktop NG shots,8 desktop SNG shots, private SNG exclusion, wrong hash, stale pose and one invalid hand; native QC and production scope')
# Rejected nested admission masks an older accepted pose.
setv('$outer','(sv_vr_weapon_pose_scope_t *)calloc(1,sizeof(sv_vr_weapon_pose_scope_t))')
setv('$client->cmd.vr_contact_received','realtime');setv('$client->lastmovetime','realtime')
cmd('call SV_BeginPrivateVRWeaponPose($p,$client,&$client->cmd,$outer)')
assert int(ev('$outer->akimbo_pose_valid'))==1
setv('$client->cmd.vr_contact_received','realtime - 0.3');fire(2,1,'rejected',refresh=False)
cmd('call SV_EndPrivateVRWeaponPoseGuarded($p,$outer,1)')
assert int(ev('sv_vr_weapon_pose_scope'))==0
print('PERIL_NESTED_PASS actual QC shot inside rejected nested scope, no older paired pose')
# Cache a successfully admitted makevectors muzzle before real QC relocation;
# then the exact pinned aim boundary must reject it, not return stale data.
setv('$client->cmd.vr_contact_received','realtime');cmd('call SV_BeginPrivateVRWeaponPose($p,$client,&$client->cmd,$outer)')
setv('$saved_xfunction','qcvm->xfunction');setv('$saved_xstatement','qcvm->xstatement')
setv('$saved_ox','qcvm->globals[20409]')
setv('qcvm->xfunction','ED_FindFunction("W_FireSpikes")');setv('qcvm->xstatement',69577)
setv('$p->v.weaponframe',1);setv('qcvm->globals[20409]',2)
for i,x in enumerate(vec('$p->v.v_angle')):setv('qcvm->globals[%d]'%(4+i),x)
cmd('call PF_makevectors()')
assert int(ev('$outer->peril_muzzle_valid'))==1, 'makevectors never cached a physical muzzle'
assert near(vec('$outer->peril_muzzle'),[body[0],body[1]-8,body[2]+22])
relocated=body[:];relocated[0]+=4
setv('*(int *)&qcvm->globals[4]','EDICT_TO_PROG($p)')
for i,x in enumerate(relocated):setv('qcvm->globals[%d]'%(7+i),x)
cmd('call PF_setorigin()')
assert int(ev('$outer->akimbo_invalidated'))==1 and int(ev('$outer->peril_muzzle_valid'))==0
setv('qcvm->xstatement',69582);setvec('$outer->stock_id1_muzzle',[111,222,333])
assert int(ev('SV_PerilAkimboAim($p,$outer->stock_id1_muzzle)'))==0, 'relocated aim reused cached physical muzzle'
assert vec('$outer->stock_id1_muzzle')==[111,222,333], 'rejected aim wrote cached output'
setv('*(int *)&qcvm->globals[4]','EDICT_TO_PROG($p)');setv('qcvm->globals[7]',1000)
cmd('call PF_aim()')
assert near([float(ev('qcvm->globals[%d]'%(1+i))) for i in range(3)],vec('pr_global_struct->v_forward')), 'ordinary aim fallback changed'
setv('qcvm->xfunction','$saved_xfunction');setv('qcvm->xstatement','$saved_xstatement');setv('qcvm->globals[20409]','$saved_ox')
fire(1,1,'rejected');cmd('call SV_EndPrivateVRWeaponPoseGuarded($p,$outer,1)')
assert near(vec('$p->v.origin'),relocated,.001),'relocation rewound by outer scope'
setvec('$p->v.origin',body);cmd('call SV_LinkEdict($p,0)')
print('PERIL_RELOCATION_PASS successful makevectors cached physical muzzle, real PF_setorigin invalidated cache, pinned aim rejected stale output, nested shot fallback origin and body relocation=pass')
# An eye-to-muzzle ray crosses the actual start-map floor. Derive expected
# backing from the real BSP trace independently of the adapter's clamp helper.
setvec('$outer->origin',[body[0],body[1],body[2]+22])
setvec('$client->cmd.vr_akimbo_muzzle[1]',[0,0,-90])
setvec('$outer->peril_muzzle',[body[0],body[1],body[2]-90])
setv('$trace','SV_Move($outer->origin,vec3_origin,vec3_origin,$outer->peril_muzzle,1,$p)')
assert float(ev('$trace.fraction'))<1 and not int(ev('$trace.startsolid')) and not int(ev('$trace.allsolid'))
expected_clamp=vec('$trace.endpos');expected_clamp[2]+=1
fire(1,1,'clamped')
setvec('$client->cmd.vr_akimbo_muzzle[1]',[0,-8,22])
print('PERIL_CLAMP_PASS native QC projectile starts at BSP-backed physical muzzle')
# Native target correction must use that same physical muzzle as PF_aim's
# initial trace; the temporary source-compensated entity origin differs.
setv('$target','ED_Alloc()');setv('$target->v.solid',2);setv('$target->v.takedamage',2)
setvec('$target->v.mins',[-1,-1,-1]);setvec('$target->v.maxs',[1,1,1]);setvec('$target->v.size',[2,2,2])
setvec('$target->v.origin',[body[0],body[1]-72,body[2]+30]);cmd('call SV_LinkEdict($target,0)')
setv('sv_aim.value',.93);fire(1,0,'target');setv('sv_aim.value',1);cmd('call ED_Free($target)')
cmd('call (void)free($outer)')
print('PERIL_NATIVE_BOUNDARIES_PASS rejected nesting, QC relocation, BSP clamp, native target correction')

end
quit 0
