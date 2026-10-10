# Native Honey gesture -> final usercmd -> loopback -> QuakeC damage proof.
# Requires a private licensed Honey profile and isolated simulated Monado.
# Only incoming controller/head samples are synthetic. Native decoded MD5 points
# are checked against the shader calculation. A disposable target is placed
# halfway along the actual native axe trace; this is not physical contact proof.
# Kills only this owned inferior; GDB --return-child-result may consequently
# return 255. Require the success marker AND successful typed result assertions.
set breakpoint pending on
set pagination off
set confirm off
set print thread-events off
set debuginfod enabled off
python
import gdb,math,json,os,time
started=time.monotonic()
def i(e): return int(gdb.parse_and_eval(e))
def f(e): return float(gdb.parse_and_eval(e))
def setv(e,v): gdb.execute('set variable %s = %s'%(e,v),to_string=True)
def call(e): return gdb.parse_and_eval(e)
def command(text):
 data=text.encode();n=i('cmd_text.cursize');assert n+len(data)<i('cmd_text.maxsize')
 gdb.selected_inferior().write_memory(i('(long)cmd_text.data')+n,data);setv('cmd_text.cursize',n+len(data))
class NoFocus(gdb.Breakpoint):
 def stop(self):
  gdb.execute('return',to_string=True);return False
NoFocus('IN_Activate',internal=True)
class Ready(gdb.Breakpoint):
 def stop(self): return time.monotonic()-started>35 or (i('cls.signon')==4 and i('tracked_aim_ready') and i('base_angles_valid'))
decoded_caches=[]
class CacheFinish(gdb.FinishBreakpoint):
 def __init__(self,out,expected): super().__init__(internal=True);self.out=out;self.expected=expected
 def stop(self):
  cache=gdb.Value(self.out).cast(gdb.lookup_type('md5stockaxe_edge_t').pointer()).dereference()
  assert int(cache['edge']['valid'])
  actual=[[float(cache['edge'][name][a]) for a in range(3)] for name in ('base','tip')]
  error=max(abs(actual[p][a]-self.expected[p][a]) for p in range(2) for a in range(3))
  assert error<.0001,(actual,self.expected,error)
  decoded_caches.append(dict(points=actual,max_error=error))
  return False
class CacheBegin(gdb.Breakpoint):
 def stop(self):
  surf=gdb.parse_and_eval('surf').dereference();format=int(surf['poseverttype'])
  vertex_type='md5vert8_t' if format==i('PV_MD5_8') else 'md5vert_t'
  count=8 if format==i('PV_MD5_8') else 4
  expected=[]
  vertices=gdb.parse_and_eval('vertices').cast(gdb.lookup_type(vertex_type).pointer())
  joints=gdb.parse_and_eval('ready_joints')
  for index in (55,54):
   vertex=vertices[index];total=sum(int(vertex['joint_weights'][j]) for j in range(count));point=[0.,0.,0.]
   for j in range(count):
    joint=joints[int(vertex['joint_indices'][j])];w=int(vertex['joint_weights'][j])/total
    xyz=[float(vertex['joint_position_'+a][j]) for a in 'xyz']
    for a in range(3):point[a]+=sum(float(joint['mat'][a*4+k])*xyz[k] for k in range(3))+float(joint['mat'][a*4+3])*w
   expected.append([point[a]*float(surf['scale'][a])+float(surf['scale_origin'][a]) for a in range(3)])
  CacheFinish(i('(long)out'),expected);return False
CacheBegin('MD5_CacheStockAxeEdge',internal=True)
ready=Ready('Host_Frame',internal=True)
gdb.Breakpoint('Host_Error',internal=True);gdb.Breakpoint('Sys_Error',internal=True)
gdb.execute('run')
assert gdb.newest_frame().name()=='Host_Frame' and i('cls.signon')==4, dict(stop=gdb.newest_frame().name(),signon=i('cls.signon'),tracked=i('tracked_aim_ready'),base=i('base_angles_valid'),stereo=i('vulkan_globals.stereo_active'),state=i('cls.state'),head=i('openxr_frame.devices[0].valid'))
ready.delete()
assert decoded_caches, 'real decoder did not produce official MD5 axe cache'
print('ACTUAL_MD5_DECODER_SHADER_POINT_PARITY_PASSED '+json.dumps(decoded_caches),flush=True)
# Spawn a disposable, unrendered damage target in this private server only.
gdb.execute('set $oldvm=qcvm',to_string=True)
call('PR_SwitchQCVM(&sv.qcvm)')
gdb.execute('set $target=ED_Alloc()',to_string=True)
setv('$target->v.health',1000);setv('$target->v.takedamage',1);setv('$target->v.solid',0)
for axis in range(3):
 setv('$target->v.origin[%d]'%axis,'svs.clients[0].edict->v.origin[%d]+%d'%(axis,24 if axis==2 else 0))
 setv('$target->v.mins[%d]'%axis,-64);setv('$target->v.maxs[%d]'%axis,64);setv('$target->v.size[%d]'%axis,128)
setv('$target->v.classname','PR_SetEngineString("gesture_fixture_target")')
gdb.execute('set $noop=ED_FindFunction("SUB_Null")',to_string=True)
for field in ('th_pain','th_die'):
 gdb.execute('set $callback=GetEdictFieldValueByName($target,"%s")'%field,to_string=True)
 assert i('$callback != 0');setv('$callback->function','$noop-sv.qcvm.functions')
call('SV_LinkEdict($target,0)')
setv('svs.clients[0].edict->v.weapon',4096)
setv('svs.clients[0].edict->v.items','(int)svs.clients[0].edict->v.items|4096')
setv('pr_global_struct->self','(int)((char *)svs.clients[0].edict-(char *)sv.qcvm.edicts)')
gdb.execute('set $ammo=ED_FindFunction("W_SetCurrentAmmo")',to_string=True);assert i('$ammo != 0')
call('PR_ExecuteProgram($ammo-sv.qcvm.functions)');call('PR_SwitchQCVM($oldvm)')
command('vr_aimmode 7\nvr_immersive_melee 1\nvr_vrik 0\nvr_lefthanded 0\nvr_world_scale 1\nvr_weapon_collision 0\nvr_crosshair 1\nvr_crosshair_depth 0\n')
# Actual XR frame owner; only the incoming controller/head sample is synthetic.
step=0;stage='warm';stage_start=time.monotonic();attacks=[];pulses=[];sources=[];command_records=[];pointer_draws=[]
# Raw controller rotation about tracking Y, composed with Index device->grip.
# Independent double Euler oracle, not production inverse constants.
def mul(a,b): return [[sum(a[r][k]*b[k][c] for k in range(3)) for c in range(3)] for r in range(3)]
def euler(x,y,z):
 x,y,z=[math.radians(v) for v in (x,y,z)]
 return mul([[math.cos(z),-math.sin(z),0],[math.sin(z),math.cos(z),0],[0,0,1]],mul([[math.cos(y),0,math.sin(y)],[0,1,0],[-math.sin(y),0,math.cos(y)]],[[1,0,0],[0,math.cos(x),-math.sin(x)],[0,math.sin(x),math.cos(x)]]))
def inject():
 global step,stage,stage_start
 step+=1;now=time.monotonic();angle=0;omega=0;x=.2
 if stage=='warm' and now-stage_start>1 and i('cl.stats[2]')>0:
  model=gdb.parse_and_eval('cl.model_precache[cl.stats[2]]->name').string()
  if model=='progs/v_axe.mdl': stage='wiggle';stage_start=now
 if stage=='wiggle': angle=(1 if step%2 else -1)*2;omega=(1 if step%2 else -1)*math.radians(200)
 if stage=='wiggle' and now-stage_start>1: stage='rest';stage_start=now
 if stage=='rest' and now-stage_start>.25: stage='swing';stage_start=now
 if stage=='swing':
  # Deliberate 18 cm translation over three 20 ms samples; fixed thereafter.
  raw_n=sum(1 for p in pulses if p.get('stage')=='swing')+1; n=min(3,raw_n)
  x=.2+.06*n
 if stage=='swing' and now-stage_start>.7: stage='done'
 setv('openxr_frame.focused',1);setv('openxr_frame.should_render',1);setv('openxr_frame.reference_changed',0)
 setv('openxr_frame.sample_id',1000+step);setv('openxr_frame.sample_time_seconds',10+step*.02)
 for h in range(2):
  raw=euler(0,angle if h==1 else 0,0);c=euler(15.392,2.071 if h else -2.071,-.303 if h else .303);grip=mul(raw,c)
  t=[0,-.015,.13];lever=[sum(raw[r][k]*t[k] for k in range(3)) for r in range(3)]
  origin=[x if h else -.2,1.3,-.3];angular=[0,omega if h else 0,0]
  velocity=[3 if stage=='swing' and h and raw_n<=3 else 0,0,0]
  spin=[angular[1]*lever[2]-angular[2]*lever[1],angular[2]*lever[0]-angular[0]*lever[2],angular[0]*lever[1]-angular[1]*lever[0]]
  for r in range(3):
   for col in range(3):setv('openxr_frame.devices[%d].matrix[%d][%d]'%(h+1,r,col),grip[r][col])
   setv('openxr_frame.devices[%d].matrix[%d][3]'%(h+1,r),origin[r]+lever[r])
   setv('openxr_frame.devices[%d].velocity[%d]'%(h+1,r),velocity[r]+spin[r]);setv('openxr_frame.devices[%d].angular_velocity[%d]'%(h+1,r),angular[r])
  for field,value in [('valid',1),('tracked',1),('connected',1),('velocity_valid',1),('angular_velocity_valid',1),('kind','VRXR_DEVICE_HAND'),('hand',h)]:setv('openxr_frame.devices[%d].%s'%(h+1,field),value)
  setv('openxr_frame.hands[%d].active'%h,1);setv('openxr_frame.hands[%d].profile'%h,'VRXR_PROFILE_INDEX');setv('openxr_frame.hands[%d].trigger'%h,1 if stage=='wiggle' else 0)
 for r in range(3):
  for col in range(3):setv('openxr_frame.devices[0].matrix[%d][%d]'%(r,col),int(r==col))
  setv('openxr_frame.devices[0].matrix[%d][3]'%r,[0,1.6,0][r])
 setv('openxr_frame.devices[0].tracked',1);setv('openxr_frame.devices[0].valid',1);setv('openxr_frame.devices[0].kind','VRXR_DEVICE_HEAD');setv('openxr_frame.devices[0].hand',-1)
 if stage=='swing':pulses.append(dict(stage=stage,step=step))
class FrameFinish(gdb.FinishBreakpoint):
 def stop(self): inject();return False
class FrameBegin(gdb.Breakpoint):
 def stop(self): FrameFinish(internal=True);return False
FrameBegin('VRXR_BeginFrame',internal=True)
class Attack(gdb.Breakpoint):
 def stop(self):
  name=gdb.parse_and_eval('qcvm->strings + qcvm->functions[fnum].s_name').string()
  if name=='W_FireAxe' or name.startswith('player_axe'): attacks.append(dict(stage=stage,step=step,time=f('sv.qcvm.time')))
  return False
Attack('PR_ExecuteProgram',internal=True)
class MergeFinish(gdb.FinishBreakpoint):
 def __init__(self,final):super().__init__(internal=True);self.final=final
 def stop(self):
  if self.return_value is not None and int(self.return_value)&1:sources.append(dict(stage=stage,step=step,final=self.final,edge_source=i('vr_input_generic_melee[1].edge_source')))
  return False
class Merge(gdb.Breakpoint):
 def stop(self): MergeFinish(bool(i('isfinal')));return False
Merge('VR_InputMergeMeleeAttack',internal=True)
class CommandFinish(gdb.FinishBreakpoint):
 def __init__(self,cmd,final):super().__init__(internal=True);self.cmd=cmd;self.final=final
 def stop(self):
  c=gdb.Value(self.cmd).cast(gdb.lookup_type('usercmd_t').pointer()).dereference()
  if (stage=='swing' and len(command_records)<15) or int(c['buttons'])&1:command_records.append(dict(step=step,final=self.final,buttons=int(c['buttons']),active=int(c['vr_active']),relative=int(c['vr_handpos_relative']),pending=i('cl.pendingcmd.vr_active'),model=i('cl.stats[2]')))
  return False
class CommandBegin(gdb.Breakpoint):
 def stop(self): CommandFinish(i('(long)cmd'),bool(i('isfinal')));return False
CommandBegin('CL_FinishMoveInternal',internal=True)
class PointerFinish(gdb.FinishBreakpoint):
 def __init__(self,cbx):super().__init__(internal=True);self.cbx=cbx
 def stop(self):
  context=gdb.Value(self.cbx).cast(gdb.lookup_type('cb_context_t').pointer()).dereference()
  stage_id=int(context['subpass_type']);variant=int(context['pipeline_variant'])
  family=gdb.parse_and_eval('graphics_pipelines[PIPELINE_COOP_NAMETAG][%d][%d]'%(stage_id,variant))
  expected=int(family['handle']);instance=family['alternatives']
  while int(instance):
   item=instance.dereference();binding=item['binding']
   if int(binding['subpass'])==int(context['subpass']) and int(context['render_pass']) in [int(binding['render_pass'][i('MAIN_RENDER_PASS_STENCIL_CLEAR')]),int(binding['render_pass'][i('MAIN_RENDER_PASS_NO_STENCIL')])]:
    expected=int(item['handle']);break
   instance=item['next']
  assert int(context['current_pipeline']['handle'])==expected and expected, (int(context['current_pipeline']['handle']),expected)
  assert int(context['current_pipeline']['layout']['handle'])==int(family['layout']['handle'])
  pointer_draws.append(step);return False
class PointerBegin(gdb.Breakpoint):
 def stop(self):
  if not i('vr_crosshair_frame.overlay'):
   assert i('cbx->subpass_type')==i('SUBPASS_MAIN')
   assert i('vr_crosshair_frame.valid') and i('whitetexture != 0')
   PointerFinish(i('(long)cbx'))
  return False
PointerBegin('R_EmitVRCrosshairQuad',internal=True)
class NativeAxeTrace(gdb.Breakpoint):
 def stop(self):
  return stage=='swing' and gdb.parse_and_eval('qcvm->strings + qcvm->xfunction->s_name').string()=='W_FireAxe'
trace_bp=NativeAxeTrace('PF_traceline',internal=True)
class Done(gdb.Breakpoint):
 def stop(self): return stage=='done' or time.monotonic()-started>65
Done('Host_Frame',internal=True)
gdb.execute('continue')
while gdb.newest_frame().name()=='PF_traceline':
 start=[f('qcvm->globals[%d]'%(4+a)) for a in range(3)];end=[f('qcvm->globals[%d]'%(7+a)) for a in range(3)]
 for a in range(3):
  setv('$target->v.origin[%d]'%a,(start[a]+end[a])/2)
  setv('$target->v.mins[%d]'%a,-4);setv('$target->v.maxs[%d]'%a,4);setv('$target->v.size[%d]'%a,8)
 setv('$target->v.solid',2);call('SV_LinkEdict($target,0)')
 print('NATIVE_AXE_TRACE_TARGET '+json.dumps(dict(start=start,end=end)),flush=True)
 trace_bp.enabled=False
 gdb.execute('continue')
result=dict(stage=stage,attacks=attacks,attack_intents=sources,steps=step,decoded_caches=decoded_caches,pointer_draws=len(pointer_draws),commands=command_records,servercmd=i('svs.clients[0].cmd.buttons'),servertime=f('sv.qcvm.time'),target_health=f('$target->v.health'),model=gdb.parse_and_eval('cl.model_precache[cl.stats[2]]->name').string())
assert stage=='done',result
assert not any(p['stage']=='wiggle' for p in sources),result
assert not any(p['stage']=='wiggle' for p in attacks),result
assert any(p['stage']=='swing' and p['final'] and p['edge_source']==1 for p in sources),result
assert pointer_draws,result
assert any(p['stage']=='swing' for p in attacks),result
assert 0<result['target_health']<1000,result
print('HONEY_NATIVE_GESTURE_ATTACK_PASSED '+json.dumps(result),flush=True)
with open(os.environ['QSVR_HONEY_RESULT'],'w') as sink:json.dump(result,sink,indent=2)
gdb.execute('kill',to_string=True)
end
quit
