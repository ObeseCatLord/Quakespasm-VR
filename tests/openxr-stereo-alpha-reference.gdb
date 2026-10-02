# Native exceptional/common alpha reference capture; actual Vulkan/Monado rendering.
# Requires the native test binary linked with tests/stereo_alpha_native_fixture.c,
# fixtures emitted by tests/prepare_stereo_alpha_native.py, private alpha.cfg,
# licensed stock e1m1 data, and an isolated simulated Monado environment.
# Set XR_STEREO_ALPHA_OUTPUT, XR_STEREO_ALPHA_GROUP (0/1), and
# XR_STEREO_ALPHA_EYE (0/1). XR_STEREO_ALPHA_GEOMETRY optionally requests the
# fixture's read-only loaded-geometry metadata export.
set pagination off
set confirm off
set print thread-events off
set debuginfod enabled off
set $started=0
set $startup=0
set $phase=0
set $frames=0
set $armed=0
python
import gdb,json,struct,os,re,subprocess
from pathlib import Path
root=Path(os.environ['XR_STEREO_ALPHA_OUTPUT'])
root.mkdir(exist_ok=True,parents=True)
def env_index(name):
 value=os.environ.get(name)
 if value not in ('0','1'):raise ValueError('%s must be exactly 0 or 1'%name)
 return int(value)
group=env_index('XR_STEREO_ALPHA_GROUP')
target_eye=env_index('XR_STEREO_ALPHA_EYE')
geometry_path=os.environ.get('XR_STEREO_ALPHA_GEOMETRY')
if geometry_path is not None and (not geometry_path or any(c in geometry_path for c in '\r\n"')):raise ValueError('XR_STEREO_ALPHA_GEOMETRY must be a nonempty console-quoted path')
def iv(s):return int(gdb.parse_and_eval(s))
def fv(s):return float(gdb.parse_and_eval(s))
def addr(s):return int(gdb.parse_and_eval('&('+s+')'))
def read_bytes(s):
 value=gdb.parse_and_eval(s)
 size=value.type.strip_typedefs().sizeof
 return bytes(gdb.selected_inferior().read_memory(addr(s),size))
def write_bytes(s,data):gdb.selected_inferior().write_memory(addr(s),data)
def copy_bytes(src,dst):write_bytes(dst,read_bytes(src))
def write(s,values):gdb.selected_inferior().write_memory(addr(s),struct.pack('='+str(len(values))+'f',*values))
def floats_from_bytes(data):
 if len(data)%4:raise AssertionError('pose byte size is not a float multiple')
 return list(struct.unpack('='+str(len(data)//4)+'f',data))
def frame_view(i,field):return 'openxr_frame.views[%d].%s'%(i,field)
def submitted_view(i,field):return "'(anonymous namespace)::g'.views[%d].%s"%(i,field)
def phase_info(phase):return dict(mode=phase//8,group=group,eye=target_eye,layer=('B','E','W','C')[(phase//2)%4],repeat=phase%2)
injected=False
runtime_fovs=None
injection_records={}
samples=[]
snapshot_serial=0
snapshots={}
presentations={}
notify_settled={}
def expected_wet_mask(mode):
 if mode==0:return 1 if group==0 else 2
 return 3 if group==target_eye else 0
def phase_inputs():
 phase=iv('$phase')
 if not iv('$armed') or not 0<=phase<16 or not notifications_expired():return None
 info=phase_info(phase);mode=info['mode'];layer=('B','E','W','C').index(info['layer'])
 alpha=128 if layer in (1,3) else 1
 water=.5 if layer in (2,3) else 0.0
 expected_mask=expected_wet_mask(mode)
 expected_exceptional=(mode==0)
 if iv('fixture_alpha_static_count')!=2:return None
 if iv('vr_mirror.value')!=target_eye+1 or fv('r_wateralpha.value')!=water:return None
 if not iv('stereo_liquid_categories_valid') or iv('stereo_wet_eye_mask')!=expected_mask:return None
 if bool(iv('stereo_alpha_exceptional'))!=expected_exceptional:return None
 for j in range(2):
  if iv('cl.static_entities[fixture_alpha_static_indices[%d]]->alpha'%j)!=alpha:return None
 return dict(phase=phase,**info,water=water,alpha=alpha,wet_mask=expected_mask,exceptional=expected_exceptional,cl_time=fv('cl.time'))
class SnapshotReceipt(gdb.Breakpoint):
 def stop(self):
  global snapshot_serial
  inputs=phase_inputs()
  if inputs is not None:
   slot=iv('slot');assert 0<=slot<2
   snapshot_serial+=1
   snapshots[slot]=dict(inputs,slot=slot,snapshot_id=snapshot_serial)
  return False
class PresentReturn(gdb.FinishBreakpoint):
 def __init__(self,slot,index,acquired,receipt):
  super().__init__(gdb.newest_frame(),internal=True)
  self.silent=True;self.slot=slot;self.index=index;self.acquired=acquired;self.receipt=receipt
 def stop(self):
  # A decrement plus no restart selects the native successful-present branch.
  inputs=phase_inputs()
  if self.receipt is not None and inputs is not None:
   r=self.receipt
   if all(r[k]==inputs[k] for k in inputs) and iv('num_images_acquired')==self.acquired-1 and iv('openxr_mirror_ready') and iv('openxr_mirror_submitted[%d]'%self.slot) and not iv('vid.restart_next_frame') and not iv('surface_lost'):
    presentations.setdefault(r['phase'],{})[r['snapshot_id']]=dict(r,image_index=self.index)
  return False
class PresentEntry(gdb.Breakpoint):
 def stop(self):
  if phase_inputs() is not None:
   slot=iv('slot');index=iv('image_index');assert 0<=slot<2
   PresentReturn(slot,index,iv('num_images_acquired'),snapshots.get(slot))
  return False
def notifications_expired():
 times=gdb.parse_and_eval('con_times')
 low,high=times.type.strip_typedefs().range()
 now=fv('realtime')
 notifytime=fv('con_notifytime.value')/(4 if fv('scr_viewsize.value')>=130 else 1)
 fade=max(fv('con_notifyfade.value')*fv('con_notifyfadetime.value'),0.0)
 return all(float(times[i])==0.0 or float(times[i])+notifytime+fade-now<=0.0 for i in range(low,high+1))
def capture_ready():
 phase=iv('$phase')
 notify_settled[phase]=notify_settled.get(phase,0)+1 if notifications_expired() and phase_inputs() is not None else 0
 return iv('$frames')>=8 and notify_settled[phase]>=8 and len(presentations.get(phase,{}))>=3

def inject():
 global injected,runtime_fovs
 phase=iv('$phase');mode=phase//8
 # A rigid 90-degree roll places the left eye below this actual water plane.
 s=1 if group==0 else -1
 position=[0,1.6,0];rotation=[[0,-s,0],[s,0,0],[0,0,1]]
 write('openxr_frame.devices[0].matrix',[a for j in range(3) for a in rotation[j]+[position[j]]])
 for eye in range(2):
  eye_position=[0,1.6+s*(.032 if eye else -.032),0]
  write(frame_view(eye,'matrix'),[a for j in range(3) for a in rotation[j]+[eye_position[j]]])
  write(submitted_view(eye,'pose'),[0,0,s*2**-.5,2**-.5]+eye_position)
 if runtime_fovs is None:
  runtime_fovs=[{field:fv(frame_view(eye,field)) for field in ('left','right','up','down')} for eye in range(2)]
 for eye in range(2):
  for field in ('left','right','up','down'):write(frame_view(eye,field),[runtime_fovs[eye][field]])
 head_matrix=[fv('openxr_frame.devices[0].matrix[%d][%d]'%(r,c)) for r in range(3) for c in range(4)]
 original_matrices=[ [fv(frame_view(eye,'matrix[%d][%d]'%(r,c))) for r in range(3) for c in range(4)] for eye in range(2)]
 original_poses=[floats_from_bytes(read_bytes(submitted_view(eye,'pose'))) for eye in range(2)]
 target_fov=dict(runtime_fovs[target_eye])
 target_pose=dict(matrix=list(original_matrices[target_eye]),submitted=original_poses[target_eye])
 other_eye=1-target_eye
 if mode==1:
  copy_bytes(frame_view(target_eye,'matrix'),frame_view(other_eye,'matrix'))
  copy_bytes(submitted_view(target_eye,'pose'),submitted_view(other_eye,'pose'))
  for field in ('left','right','up','down'):write(frame_view(other_eye,field),[target_fov[field]])
 render_matrices=[ [fv(frame_view(eye,'matrix[%d][%d]'%(r,c))) for r in range(3) for c in range(4)] for eye in range(2)]
 render_poses=[floats_from_bytes(read_bytes(submitted_view(eye,'pose'))) for eye in range(2)]
 injection_records[phase]=dict(original_head_matrix=head_matrix,original_view_matrices=original_matrices,original_submitted_poses=original_poses,original_fovs=runtime_fovs,target_fov=target_fov,target_pose=target_pose,render_view_matrices=render_matrices,render_submitted_poses=render_poses)
 if not injected:
  gdb.execute('call (void)V_ResetTrackedAim()')
  gdb.execute('call (void)R_InvalidateStereoReference()')
  injected=True
consumer_records={}
def uniform_read(descriptor,offset):
 indices=[i for i in range(2) if iv('ubo_descriptor_sets[%d]'%i)==descriptor]
 assert len(indices)==1,'shader descriptor not in native current mapped UBO pages'
 i=indices[0]
 assert 0<=offset and offset+160<=iv('current_dyn_uniform_buffer_size')
 base=iv('dyn_uniform_buffers[%d].data'%i);assert base
 return list(struct.unpack('=40f',gdb.selected_inferior().read_memory(base+offset,160)))
def consumer_context(cbx):
 ptr=int(cbx)
 stage=None
 for j,name in enumerate(('SCBX_ALPHA_ENTITIES_ACROSS_WATER','SCBX_ALPHA_ENTITIES')):
  if ptr==iv('vulkan_globals.secondary_cb_contexts[%s]'%name):stage=j
 assert stage is not None,'unknown alpha consumer context'
 prefix='((cb_context_t*)%d)'%ptr
 override=iv(prefix+'->scene_descriptor_override');offset=iv(prefix+'->scene_uniform_offset_override')
 mode=iv('$phase')//8
 if mode:
  assert not override and not offset and not iv('stereo_alpha_exceptional')
  descriptor=iv('vulkan_globals.stereo_scene_descriptor_set');offset=iv('vulkan_globals.stereo_scene_uniform_offset')
 else:
  pairs=[(iv('stereo_alpha_eye_descriptor_set[%d]'%i),iv('stereo_alpha_eye_uniform_offset[%d]'%i)) for i in range(2)]
  assert (override,offset) in pairs and pairs[0]!=pairs[1]
  descriptor=override
 u=uniform_read(descriptor,offset)
 eye=target_eye
 assert u[eye*16:eye*16+16]==[float(gdb.parse_and_eval('vulkan_globals.stereo_clip_from_center[%d][%d]'%(eye,j))) for j in range(16)]
 assert u[32+eye*4:35+eye*4]==[float(gdb.parse_and_eval('vulkan_globals.stereo_eye_offset[%d][%d]'%(eye,j))) for j in range(3)]
 if mode:assert u[35]==0 and u[39]==0
 else:
  chosen=pairs.index((override,offset));assert u[35+chosen*4]==0 and u[35+(1-chosen)*4]==1
 return stage,u[35+eye*4],u
class AliasReturn(gdb.FinishBreakpoint):
 def __init__(self,phase,ptr,before,item):
  super().__init__(gdb.newest_frame(),internal=True);self.silent=True
  self.phase=phase;self.ptr=ptr;self.before=before;self.item=item
 def stop(self):
  if phase_inputs() is not None and iv('$phase')==self.phase:
   after=int(gdb.Value(self.ptr).cast(gdb.lookup_type('int').pointer()).dereference())
   assert after-self.before==4,'fixture alias rejected or wrong geometry'
   records=consumer_records.setdefault(self.phase,{})
   key=(self.item['stage'],self.item['entity'],self.item['excluded'])
   if key in records:
    assert records[key]['uniform']==self.item['uniform']
    records[key]['count']+=1
   else:records[key]=dict(self.item,count=1)
  return False
class AliasConsumer(gdb.Breakpoint):
 def stop(self):
  inputs=phase_inputs()
  if inputs is None or inputs['alpha']!=128:return False
  ent=iv('e');ids=[iv('cl.static_entities[fixture_alpha_static_indices[%d]]'%j) for j in range(2)]
  if ent not in ids:return False
  stage,excluded,u=consumer_context(gdb.parse_and_eval('cbx'));category=ent==ids[0]
  # Literal legacy expectations, independent of the production mask equations.
  target_wet=(group,target_eye) in ((0,0),(1,1))
  expected_stage={(True,False):0,(True,True):1,(False,True):0,(False,False):1}[(target_wet,category)]
  if not excluded:assert stage==expected_stage,'category in wrong target-eye water stage'
  item=dict(stage=stage,entity='wet' if category else 'dry',excluded=int(excluded),uniform=u,mode=iv('$phase')//8)
  ptr=iv('aliaspolys');before=int(gdb.Value(ptr).cast(gdb.lookup_type('int').pointer()).dereference())
  AliasReturn(iv('$phase'),ptr,before,item)
  return False

def observe():
 phase=iv('$phase');info=phase_info(phase);mode=info['mode'];layer=info['layer']
 enabled=layer in ('E','C');water=.5 if layer in ('W','C') else 0.0
 wet_mask=expected_wet_mask(mode);exceptional=(mode==0)
 assert iv('fixture_alpha_static_count')==2 and iv('cl.num_statics')>=2
 assert iv('cl.paused') and iv('vulkan_globals.sample_count')==4 and iv('r_ssao.value')==1 and iv('r_oit.value')==0
 assert fv('r_wateralpha.value')==water and iv('vr_mirror.value')==target_eye+1
 assert iv('stereo_liquid_categories_valid') and iv('stereo_wet_eye_mask')==wet_mask
 assert bool(iv('stereo_alpha_exceptional'))==exceptional
 identities=[]
 for j in range(2):
  ent='cl.static_entities[fixture_alpha_static_indices[%d]]'%j
  assert iv(ent+'->alpha')==(128 if enabled else 1)
  assert iv(ent+'->baseline.alpha')==iv(ent+'->alpha') and iv(ent+'->netstate.alpha')==iv(ent+'->alpha')
  assert iv(ent+'->model')==iv('cl.model_precache[fixture_alpha_models[%d]]'%j)
  gdb.execute('set scheduler-locking on')
  try:gdb.execute('set $entityleaf=(mleaf_t*)Mod_PointInLeaf(%s->origin,cl.worldmodel)'%ent)
  finally:gdb.execute('set scheduler-locking off')
  contents=iv('$entityleaf->contents');assert contents==(-3 if j==0 else -1)
  header='((aliashdr_t*)%s->model->extradata[PV_QUAKE1])'%ent
  skin=iv(header+'->texels[0][0]');fb=iv(header+'->fbtextures[0][0]')
  assert skin==255 and fb and iv('gl_fullbrights.value')==1
  identities.append(dict(skin_guard=skin,fullbright=fb,index=iv('fixture_alpha_static_indices[%d]'%j),contents=contents,alpha=iv(ent+'->alpha')))
 if enabled:assert iv('cl_numvisedicts_alpha_underwater')>=1 and iv('cl_numvisedicts_alpha_overwater')>=1
 windows=subprocess.run(['xprop','-root','_NET_CLIENT_LIST'],capture_output=True,text=True,check=True,timeout=5).stdout
 owned=[]
 for wid in re.findall(r'0x[0-9a-f]+',windows):
  owner=subprocess.run(['xprop','-id',wid,'_NET_WM_PID'],capture_output=True,text=True,check=True,timeout=5).stdout
  if re.search(r'=\s*%d\s*$'%gdb.selected_inferior().pid,owner.strip()):owned.append(wid)
 assert len(owned)==1
 name='mode%d-group%d-eye%d-%s-repeat%d.png'%(mode,group,target_eye,layer,info['repeat'])
 subprocess.run(['import','-window',owned[0],str(root/name)],check=True,timeout=10)
 calls=list(consumer_records.get(phase,{}).values())
 if enabled:
  assert len(calls)==(2 if mode else 4),'missing accepted native alias consumer tuples'
  assert {c['entity'] for c in calls if not c['excluded']}=={'wet','dry'}
 else:assert not calls
 time=fv('cl.time')
 if samples:assert time==samples[0]['cl_time']
 receipts=list(presentations.get(phase,{}).values());assert len(receipts)>=3
 transfer=dict(gamma=fv('vid_gamma.value'),contrast=fv('vid_contrast.value'),palette=iv('vid_palettize.value'),waterwarp=iv('r_waterwarp.value'),polyblend=iv('gl_polyblend.value'),blend=[fv('v_blend[%d]'%i) for i in range(4)],console=fv('scr_con_current'),forced_console=iv('con_forcedup'),native_extent=[iv('vid.width'),iv('vid.height')],mirror_extent=[iv('openxr_mirror_extent.width'),iv('openxr_mirror_extent.height')])
 assert transfer['gamma']==1 and transfer['contrast']==1 and transfer['palette']==0 and transfer['waterwarp']==0
 matrices=dict(center_clip=[fv('vulkan_globals.view_projection_matrix[%d]'%i) for i in range(16)],eye_clip=[[fv('vulkan_globals.stereo_clip_from_center[%d][%d]'%(eye,i)) for i in range(16)] for eye in range(2)])
 injection=injection_records.get(phase);assert injection is not None
 samples.append(dict(phase=phase,mode=mode,group=group,eye=target_eye,layer=layer,repeat=info['repeat'],image=name,target_fov=injection['target_fov'],target_pose=injection['target_pose'],original_head_matrix=injection['original_head_matrix'],original_view_matrices=injection['original_view_matrices'],original_submitted_poses=injection['original_submitted_poses'],original_fovs=injection['original_fovs'],render_view_matrices=injection['render_view_matrices'],render_submitted_poses=injection['render_submitted_poses'],identities=identities,consumer_calls=calls,notifications_expired=notifications_expired(),notify_settled_frames=notify_settled[phase],cl_time=time,presentations=receipts,transfer=transfer,matrices=matrices,formats=dict(scene=iv('vulkan_globals.color_format'),xr=iv('vulkan_globals.stereo_color_format'),mirror=iv('vulkan_globals.swap_chain_format')),wet_mask=iv('stereo_wet_eye_mask'),exceptional=iv('stereo_alpha_exceptional'),alpha_under=iv('cl_numvisedicts_alpha_underwater'),alpha_over=iv('cl_numvisedicts_alpha_overwater')))
 (root/'layers.json').write_text(json.dumps(dict(status='passed' if phase==15 else 'running',samples=samples),indent=2)+'\n')
 print('ALPHA_LAYER_REFERENCE_CAPTURE',phase,name,flush=True)

end
break VRXR_StereoClip if $started && cls.signon == 4 && openxr_frame.should_render
commands 1
 silent
 python inject()
 continue
end
break *GL_EndXRFrame
commands 2
 silent
 if !$started && vulkan_globals.stereo_active
  set $startup=$startup+1
  if $startup == 8
   call (void)Fixture_AlphaSceneRegister()
   call (void)Cbuf_AddText("map e1m1\n")
   set $started=1
  end
 else
  if cls.signon == 4 && vulkan_globals.stereo_active
   set $frames=$frames+1
   if !$armed
    if $frames == 4
     python
geometry=geometry_path
startup_text='god\nsetpos 836 832 -322 0 90 0\nfixture_alpha_init\n'
if geometry is not None:startup_text+='fixture_alpha_geometry "%s"\n'%geometry
gdb.execute('call (void)Cbuf_AddText(%s)'%json.dumps(startup_text))
     end
    end
    if $frames == 20
     python
startup_text='pause\nfixture_alpha_opacity 0\nr_wateralpha 0\nvr_mirror %d\n'%(target_eye+1)
gdb.execute('call (void)Cbuf_AddText(%s)'%json.dumps(startup_text))
     end
     set $frames=0
     set $armed=1
    end
   else
    if $frames >= 2000
     python assert capture_ready(), "native matching presentation receipts or notification expiry unavailable"
    end
    python gdb.set_convenience_variable("capture_ready",int(capture_ready()))
    if $capture_ready
     python observe()
     set $frames=0
     set $phase=$phase+1
     if $phase==16
      printf "ALPHA_LAYER_REFERENCE_NATIVE_PASSED\n"
      call (void)Cbuf_AddText("quit\n")
      disable 1 2
     else
      python
phase=iv('$phase');layer=(phase//2)%4
command='fixture_alpha_opacity %d\nr_wateralpha %g\nvr_mirror %d\n'%(int(layer in (1,3)),.5 if layer in (2,3) else 0,target_eye+1)
gdb.execute('call (void)Cbuf_AddText(%s)'%json.dumps(command))
      end
     end
    end
   end
  end
 end
 continue
end
break Host_Error
break Sys_Error
python
# Optimized snapshot helper is inlined; its successful copy/barrier tail is observable.
snapshot_observer=SnapshotReceipt("gl_vidsdl.c:5700",internal=True)
snapshot_observer.silent=True
present_observer=PresentEntry("*GL_SubmitXRMirror",internal=True)
present_observer.silent=True
alias_observer=AliasConsumer('*R_DrawAliasModel',internal=True)
alias_observer.silent=True
end
run
python
if gdb.selected_inferior().pid or iv('$phase')!=16 or iv('$_exitcode')!=0:gdb.execute('quit 1')
end
quit 0
