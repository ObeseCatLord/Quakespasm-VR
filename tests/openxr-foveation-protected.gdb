# Native KHR protected-output capture. One invocation captures one target eye;
# use separate empty output directories for eye 0 and eye 1.
# Requires the assertion-enabled alpha-parser fixture, assets from
# prepare_foveation_protected_native.py, private licensed e1m1 data, and an
# isolated simulated OpenXR runtime. This observer never calls Vulkan APIs.
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
import gdb,hashlib,json,math,os,re,struct,subprocess
from pathlib import Path

root=Path(os.environ['XR_FOVEATION_PROTECTED_OUTPUT']).resolve()
root.mkdir(parents=True,exist_ok=True)
if any(root.iterdir()):raise ValueError('capture output directory must be empty')
def env_eye():
 value=os.environ.get('XR_FOVEATION_PROTECTED_EYE')
 if value not in ('0','1'):raise ValueError('XR_FOVEATION_PROTECTED_EYE must be 0 or 1')
 return int(value)
eye=env_eye()
ENTALPHA_DEFAULT=0
ENTALPHA_ZERO=1
def iv(s):return int(gdb.parse_and_eval(s))
def fv(s):return float(gdb.parse_and_eval(s))
def addr(s):return int(gdb.parse_and_eval('&('+s+')'))
def read_bytes_at(pointer,size):
 if not pointer or size<0:raise AssertionError('invalid native byte range')
 return bytes(gdb.selected_inferior().read_memory(pointer,size))
def read_bytes(s):
 value=gdb.parse_and_eval(s)
 return bytes(gdb.selected_inferior().read_memory(addr(s),value.type.strip_typedefs().sizeof))
def write(s,values):
 gdb.selected_inferior().write_memory(addr(s),struct.pack('='+str(len(values))+'f',*values))
def f32s(data):
 if len(data)%4:raise AssertionError('pose byte size is not a float multiple')
 return list(struct.unpack('='+str(len(data)//4)+'f',data))
def frame_view(i,field):return 'openxr_frame.views[%d].%s'%(i,field)
def submitted_view(i,field):return "'(anonymous namespace)::g'.views[%d].%s"%(i,field)
def phase_info(phase):
 return dict(mode=phase//4,eye=eye,layer=('B','E')[(phase//2)%2],repeat=phase%2)
def mode_name(mode):return 'off' if mode==0 else 'fixed'
def phase_inputs():
 phase=iv('$phase')
 if not iv('$armed') or not 0<=phase<8 or not notifications_expired():return None
 info=phase_info(phase);mode=info['mode'];layer=info['layer']
 expected_alpha=ENTALPHA_ZERO if layer=='B' else ENTALPHA_DEFAULT
 if iv('fixture_alpha_static_count')!=2:return None
 if iv('vr_mirror.value')!=eye+1 or fv('r_wateralpha.value')!=0:return None
 if iv('vr_foveation.value')!=mode:return None
 if bool(iv('vulkan_globals.openxr_fragment_shading_rate_active'))!=(mode==1):return None
 for j in range(2):
  if iv('cl.static_entities[fixture_alpha_static_indices[%d]]->alpha'%j)!=expected_alpha:return None
 return dict(phase=phase,**info,cl_time=fv('cl.time'))

injected=False
runtime_fovs=None
injection_records={}
samples=[]
snapshot_serial=0
snapshots={}
presentations={}
notify_settled={}
alias_admissions={}
alias_draws={}
world_rate_calls={}
pipeline_creates=[]
fixture_vertex_buffers=set()

def notifications_expired():
 times=gdb.parse_and_eval('con_times');low,high=times.type.strip_typedefs().range()
 now=fv('realtime')
 notifytime=fv('con_notifytime.value')/(4 if fv('scr_viewsize.value')>=130 else 1)
 fade=max(fv('con_notifyfade.value')*fv('con_notifyfadetime.value'),0.0)
 return all(float(times[i])==0.0 or float(times[i])+notifytime+fade-now<=0.0 for i in range(low,high+1))

def capture_ready():
 phase=iv('$phase')
 notify_settled[phase]=notify_settled.get(phase,0)+1 if notifications_expired() and phase_inputs() is not None else 0
 return iv('$frames')>=8 and notify_settled[phase]>=8 and len(presentations.get(phase,{}))>=3

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
  inputs=phase_inputs()
  if self.receipt is not None and inputs is not None:
   receipt=self.receipt
   if all(receipt[k]==inputs[k] for k in inputs) and iv('num_images_acquired')==self.acquired-1 and iv('openxr_mirror_ready') and iv('openxr_mirror_submitted[%d]'%self.slot) and not iv('vid.restart_next_frame') and not iv('surface_lost'):
    presentations.setdefault(receipt['phase'],{})[receipt['snapshot_id']]=dict(receipt,image_index=self.index)
  return False

class PresentEntry(gdb.Breakpoint):
 def stop(self):
  if phase_inputs() is not None:
   slot=iv('slot');index=iv('image_index');assert 0<=slot<2
   PresentReturn(slot,index,iv('num_images_acquired'),snapshots.get(slot))
  return False

def inject():
 global injected,runtime_fovs
 phase=iv('$phase')
 source_head=[fv('openxr_frame.devices[0].matrix[%d][%d]'%(r,c)) for r in range(3) for c in range(4)]
 source_head_flags=dict(valid=iv('openxr_frame.devices[0].valid'),tracked=iv('openxr_frame.devices[0].tracked'))
 source_gaze_flags=dict(valid=iv('openxr_frame.gaze.valid'),tracked=iv('openxr_frame.gaze.tracked'),sample_time_known=iv('openxr_frame.gaze.sample_time_known'))
 rotation=[[0,-1,0],[1,0,0],[0,0,1]]
 position=[0,1.6,0]
 write('openxr_frame.devices[0].matrix',[v for r in range(3) for v in rotation[r]+[position[r]]])
 current_fovs=[{field:fv(frame_view(i,field)) for field in ('left','right','up','down')} for i in range(2)]
 if runtime_fovs is None:runtime_fovs=current_fovs
 else:assert current_fovs==runtime_fovs,'runtime view FOV changed during capture'
 source_matrices=[ [fv(frame_view(i,'matrix[%d][%d]'%(r,c))) for r in range(3) for c in range(4)] for i in range(2)]
 source_poses=[f32s(read_bytes(submitted_view(i,'pose'))) for i in range(2)]
 view_rotation=[[0,-1,0],[1,0,0],[0,0,1]]
 for i in range(2):
  side=-1 if i==0 else 1
  eye_position=[0,1.6+side*.032,0]
  write(frame_view(i,'matrix'),[v for r in range(3) for v in view_rotation[r]+[eye_position[r]]])
  write(submitted_view(i,'pose'),[0,0,2**-.5,2**-.5]+eye_position)
 render_matrices=[ [fv(frame_view(i,'matrix[%d][%d]'%(r,c))) for r in range(3) for c in range(4)] for i in range(2)]
 render_poses=[f32s(read_bytes(submitted_view(i,'pose'))) for i in range(2)]
 injection_records[phase]=dict(source_head=source_head,source_head_flags=source_head_flags,source_gaze_flags=source_gaze_flags,source_view_matrices=source_matrices,source_submitted_poses=source_poses,source_fovs=runtime_fovs,controlled_head=[v for r in range(3) for v in rotation[r]+[position[r]]],controlled_view_matrices=render_matrices,controlled_submitted_poses=render_poses)
 if not injected:
  gdb.execute('call (void)V_ResetTrackedAim()')
  gdb.execute('call (void)R_InvalidateStereoReference()')
  injected=True

class AliasAdmissionReturn(gdb.FinishBreakpoint):
 def __init__(self,phase,category,alpha,before,ptr):
  super().__init__(gdb.newest_frame(),internal=True);self.silent=True
  self.phase=phase;self.category=category;self.alpha=alpha;self.before=before;self.ptr=ptr
 def stop(self):
  if phase_inputs() is not None and iv('$phase')==self.phase:
   after=int(gdb.Value(self.ptr).cast(gdb.lookup_type('int').pointer()).dereference())
   alias_admissions.setdefault(self.phase,[]).append(dict(model=self.category,alpha=self.alpha,triangles=after-self.before))
  return False

class AliasAdmission(gdb.Breakpoint):
 def stop(self):
  inputs=phase_inputs()
  if inputs is None:return False
  name=gdb.parse_and_eval('e->model->name').string()
  category={'progs/vr_alpha_wet.mdl':'wet','progs/vr_alpha_dry.mdl':'dry'}.get(name)
  if category is None:return False
  alpha_value=iv('e->alpha');ptr=addr('(*aliaspolys)')
  before=int(gdb.Value(ptr).cast(gdb.lookup_type('int').pointer()).dereference())
  AliasAdmissionReturn(inputs['phase'],category,alpha_value,before,ptr)
  return False

class AliasDraw(gdb.Breakpoint):
 def stop(self):
  inputs=phase_inputs()
  if inputs is None or inputs['layer']!='E':return False
  for j in range(2):
   ent='cl.static_entities[fixture_alpha_static_indices[%d]]'%j
   header='((aliashdr_t*)%s->model->extradata[PV_QUAKE1])'%ent
   buffer=iv(header+'->vertex_buffer')
   if buffer:fixture_vertex_buffers.add(buffer)
  vertex_buffer=iv('state->vertex_buffer')
  if vertex_buffer not in fixture_vertex_buffers:return False
  count=iv('count')
  alpha_values=[fv('instances[%d].entalpha'%i) for i in range(count)]
  flags=[iv('instances[%d].flags'%i) for i in range(count)]
  record=dict(vertex_buffer=vertex_buffer,pipeline=iv('state->pipeline.handle'),index_count=iv('state->index_count'),instance_count=count,alphas=alpha_values,flags=flags)
  alias_draws.setdefault(inputs['phase'],[]).append(record)
  return False

class AliasPipelineReturn(gdb.FinishBreakpoint):
 def __init__(self,name,destination,record):
  super().__init__(gdb.newest_frame(),internal=True);self.silent=True
  self.name=name;self.destination=destination;self.record=record
 def stop(self):
  pipeline=gdb.Value(self.destination).cast(gdb.lookup_type('vulkan_pipeline_t').pointer()).dereference()
  item=dict(self.record,name=self.name,handle=int(pipeline['handle']))
  pipeline_creates.append(item)
  return False

class AliasPipelineCreate(gdb.Breakpoint):
 def stop(self):
  name=gdb.parse_and_eval('name').string()
  if name not in ('alias 0','alias_main_oit 0'):return False
  count=iv('infos->dynamic_state.dynamicStateCount')
  states=[iv('infos->dynamic_states[%d]'%i) for i in range(count)]
  pnext=iv('infos->graphics_pipeline.pNext')
  record=dict(active=iv('vulkan_globals.openxr_fragment_shading_rate_active'),dynamic_states=states,graphics_pnext_null=(pnext==0))
  destination=int(gdb.parse_and_eval('pipeline'))
  AliasPipelineReturn(name,destination,record)
  return False

class WorldRateReturn(gdb.FinishBreakpoint):
 def __init__(self,phase,eligible,depth,active):
  super().__init__(gdb.newest_frame(),internal=True);self.silent=True
  self.phase=phase;self.eligible=eligible;self.depth=depth;self.active=active
 def stop(self):
  if phase_inputs() is not None and iv('$phase')==self.phase:
   item=dict(eligible=self.eligible,depth_only=self.depth,active=self.active,command_submitted=bool(self.active and not self.depth))
   world_rate_calls.setdefault(self.phase,[]).append(item)
  return False

class WorldRateEntry(gdb.Breakpoint):
 def stop(self):
  inputs=phase_inputs()
  if inputs is not None:
   depth=iv('cbx->depth_only');active=iv('vulkan_globals.openxr_fragment_shading_rate_active')
   WorldRateReturn(inputs['phase'],int(gdb.parse_and_eval('eligible')),depth,active)
  return False

def rate_map_observation(mode):
 active=iv('vulkan_globals.openxr_fragment_shading_rate_active')
 available=iv('vulkan_globals.openxr_fragment_shading_rate_available')
 if mode==0:
  assert not active and not iv('fragment_shading_rate_image') and not iv('fragment_shading_rate_map') and not iv('fragment_shading_rate_map_size')
  return dict(available=available,active=False,image_present=False,render_extent=[iv('vid.render_width'),iv('vid.render_height')],map_extent=[0,0],texel_size=[iv('vulkan_globals.openxr_fragment_shading_rate_texel_size.width'),iv('vulkan_globals.openxr_fragment_shading_rate_texel_size.height')],layers=0,generated_hex='',uploaded_hex='',upload_equal=False)
 assert available and active and iv('fragment_shading_rate_image')
 width=iv('fragment_shading_rate_image_extent.width');height=iv('fragment_shading_rate_image_extent.height');layers=iv('fragment_shading_rate_image_layers')
 size=iv('fragment_shading_rate_map_size');expected=width*height*layers
 assert width>0 and height>0 and layers in (1,2) and size==expected
 generated=read_bytes_at(iv('fragment_shading_rate_map'),size)
 uploaded=read_bytes_at(iv('fragment_shading_rate_uploaded_map'),size)
 return dict(available=available,active=True,image_present=True,image_initialized=bool(iv('fragment_shading_rate_image_initialized')),format='VK_FORMAT_R8_UINT',render_extent=[iv('vid.render_width'),iv('vid.render_height')],map_extent=[width,height],texel_size=[iv('vulkan_globals.openxr_fragment_shading_rate_texel_size.width'),iv('vulkan_globals.openxr_fragment_shading_rate_texel_size.height')],layers=layers,selected_layer=eye if layers==2 else 0,generated_hex=generated.hex(),uploaded_hex=uploaded.hex(),generated_sha256=hashlib.sha256(generated).hexdigest(),uploaded_sha256=hashlib.sha256(uploaded).hexdigest(),upload_equal=generated==uploaded)

def observe():
 phase=iv('$phase');info=phase_info(phase);mode=info['mode'];layer=info['layer'];enabled=layer=='E'
 assert iv('fixture_alpha_static_count')==2 and iv('cl.num_statics')>=2
 assert iv('cl.paused') and iv('vulkan_globals.sample_count')==4 and iv('r_ssao.value')==1 and iv('r_oit.value')==1
 assert iv('vr_foveation.value')==mode and fv('r_wateralpha.value')==0 and iv('vr_mirror.value')==eye+1
 identities=[]
 for j in range(2):
  ent='cl.static_entities[fixture_alpha_static_indices[%d]]'%j
  expected_alpha=ENTALPHA_ZERO if not enabled else ENTALPHA_DEFAULT
  assert iv(ent+'->alpha')==expected_alpha and iv(ent+'->baseline.alpha')==expected_alpha and iv(ent+'->netstate.alpha')==expected_alpha
  assert iv(ent+'->model')==iv('cl.model_precache[fixture_alpha_models[%d]]'%j)
  header='((aliashdr_t*)%s->model->extradata[PV_QUAKE1])'%ent
  skin=iv(header+'->texels[0][0]');fb=iv(header+'->fbtextures[0][0]')
  assert skin==255 and fb and iv('gl_fullbrights.value')==1
  vertex_buffer=iv(header+'->vertex_buffer');assert vertex_buffer
  fixture_vertex_buffers.add(vertex_buffer)
  identities.append(dict(name=('wet','dry')[j],skin_guard=skin,fullbright=fb,alpha=expected_alpha,vertex_buffer=vertex_buffer,index_count=iv(header+'->numindexes')))
 assert fv('vid_gamma.value')==1 and fv('vid_contrast.value')==1 and iv('vid_palettize.value')==0 and iv('r_waterwarp.value')==0 and iv('gl_polyblend.value')==0
 assert notifications_expired() and notify_settled[phase]>=8
 receipts=list(presentations.get(phase,{}).values());assert len(receipts)>=3
 calls=list(alias_admissions.get(phase,[]));draws=list(alias_draws.get(phase,[]));world_calls=list(world_rate_calls.get(phase,[]))
 if enabled:
  for name in ('wet','dry'):assert any(c['model']==name and c['triangles']==4 and c['alpha']==ENTALPHA_DEFAULT for c in calls)
  assert len(draws)>=2 and all(d['instance_count']>0 and d['index_count']==12 and all(a==1.0 for a in d['alphas']) for d in draws)
 else:assert not draws
 rate_map=rate_map_observation(mode)
 if mode==1:assert any(c['eligible'] and not c['depth_only'] and c['command_submitted'] for c in world_calls)
 owned=[]
 for wid in re.findall(r'0x[0-9a-f]+',subprocess.run(['xprop','-root','_NET_CLIENT_LIST'],capture_output=True,text=True,check=True,timeout=5).stdout):
  owner=subprocess.run(['xprop','-id',wid,'_NET_WM_PID'],capture_output=True,text=True,check=True,timeout=5).stdout
  if re.search(r'=\s*%d\s*$'%gdb.selected_inferior().pid,owner.strip()):owned.append(wid)
 assert len(owned)==1
 name='eye%d-%s-%s-repeat%d.png'%(eye,mode_name(mode),layer,info['repeat'])
 subprocess.run(['import','-window',owned[0],str(root/name)],check=True,timeout=10)
 time=fv('cl.time')
 if samples:assert time==samples[0]['cl_time']
 transfer=dict(gamma=fv('vid_gamma.value'),contrast=fv('vid_contrast.value'),palette=iv('vid_palettize.value'),waterwarp=iv('r_waterwarp.value'),polyblend=iv('gl_polyblend.value'),blend=[fv('v_blend[%d]'%i) for i in range(4)],console=fv('scr_con_current'),forced_console=iv('con_forcedup'),native_extent=[iv('vid.width'),iv('vid.height')],render_extent=[iv('vid.render_width'),iv('vid.render_height')],mirror_extent=[iv('openxr_mirror_extent.width'),iv('openxr_mirror_extent.height')])
 injection=injection_records.get(phase);assert injection is not None
 matrices=dict(center_clip=[fv('vulkan_globals.view_projection_matrix[%d]'%i) for i in range(16)],eye_clip=[[fv('vulkan_globals.stereo_clip_from_center[%d][%d]'%(i,j)) for j in range(16)] for i in range(2)])
 samples.append(dict(phase=phase,mode=mode,eye=eye,layer=layer,repeat=info['repeat'],image=name,cl_time=time,paused=bool(iv('cl.paused')),r_oit=fv('r_oit.value'),r_ssao=fv('r_ssao.value'),sample_count=iv('vulkan_globals.sample_count'),vr_foveation=fv('vr_foveation.value'),runtime_views_accepted=bool(iv('openxr_frame.should_render')),runtime_source_head_matrix=injection['source_head'],runtime_head_flags=injection['source_head_flags'],runtime_gaze_flags=injection['source_gaze_flags'],source_view_matrices=injection['source_view_matrices'],source_submitted_poses=injection['source_submitted_poses'],source_fovs=injection['source_fovs'],controlled_head=injection['controlled_head'],controlled_view_matrices=injection['controlled_view_matrices'],controlled_submitted_poses=injection['controlled_submitted_poses'],identities=identities,alias_admissions=calls,alias_draws=draws,alias_pipeline_creates=pipeline_creates,world_rate_calls=world_calls,rate_map=rate_map,notifications_expired=True,notify_settled_frames=notify_settled[phase],presentations=receipts,transfer=transfer,matrices=matrices,formats=dict(scene=iv('vulkan_globals.color_format'),xr=iv('vulkan_globals.stereo_color_format'),mirror=iv('vulkan_globals.swap_chain_format'))))
 (root/'layers.json').write_text(json.dumps(dict(schema=1,status='passed' if phase==7 else 'running',eye=eye,samples=samples),indent=2)+'\n')
 print('FOVEATION_PROTECTED_NATIVE_CAPTURE',phase,name,flush=True)
end

break VRXR_StereoClip if $started && cls.signon == 4 && openxr_frame.should_render
commands
 silent
 python inject()
 continue
end
break *GL_EndXRFrame
commands
 silent
 if !$started && vulkan_globals.stereo_active
  set $startup=$startup+1
  if $startup == 8
   call (void)Fixture_AlphaSceneRegister()
   set scheduler-locking on
   set $scene_path=(char*)malloc(512)
   python
scene=str(root/'geometry.json')
if any(c in scene for c in '\r\n"'):raise ValueError('capture path cannot be console-quoted')
payload=('god\nsetpos 836 832 -322 0 90 0\nfixture_alpha_init\nfixture_alpha_geometry "%s"\n'%scene).encode()+b'\0'
gdb.selected_inferior().write_memory(int(gdb.parse_and_eval('$scene_path')),payload)
   end
   set scheduler-locking off
   call (void)Cbuf_AddText($scene_path)
   set $started=1
  end
 else
  if cls.signon == 4 && vulkan_globals.stereo_active
   set $frames=$frames+1
   if !$armed
    if $frames == 20
     python
startup='pause\nvr_foveation 0\nfixture_alpha_opacity 0\nr_wateralpha 0\nvr_mirror %d\n'%(eye+1)
gdb.execute('call (void)Cbuf_AddText(%s)'%json.dumps(startup))
     end
     set $frames=0
     set $armed=1
    end
   else
    if $frames >= 2000
     python assert capture_ready(), 'bounded wait expired before matching image receipts and quiet notifications'
    end
    python gdb.set_convenience_variable('capture_ready',int(capture_ready()))
    if $capture_ready
     python observe()
     set $frames=0
     set $phase=$phase+1
     if $phase==8
      printf "FOVEATION_PROTECTED_NATIVE_CAPTURE_COMPLETE\n"
      call (void)Cbuf_AddText("quit\n")
      disable 1 2
     else
      python
phase=iv('$phase');info=phase_info(phase)
command='fixture_alpha_opacity %d\nr_wateralpha 0\nvr_mirror %d\n'%(0 if info['layer']=='B' else 2,eye+1)
if phase==4:command+='vr_foveation 1\n'
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
snapshot_observer=SnapshotReceipt('gl_vidsdl.c:5700',internal=True)
snapshot_observer.silent=True
present_observer=PresentEntry('*GL_SubmitXRMirror',internal=True)
present_observer.silent=True
admission_observer=AliasAdmission('*R_DrawAliasModel',internal=True)
admission_observer.silent=True
draw_observer=AliasDraw('*GL_DrawAliasInstances',internal=True)
draw_observer.silent=True
pipeline_observer=AliasPipelineCreate('*R_CreateGraphicsPipeline',internal=True)
pipeline_observer.silent=True
world_rate_observer=WorldRateEntry('*R_SetWorldFragmentShadingRate',internal=True)
world_rate_observer.silent=True
end
run
python
if gdb.selected_inferior().pid or iv('$phase')!=8 or iv('$_exitcode')!=0:gdb.execute('quit 1')
end
quit 0
