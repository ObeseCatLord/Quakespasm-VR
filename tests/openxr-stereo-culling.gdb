# Adapter over the native alpha receipt/capture recipe. No renderer/state replacement.
# XR_STEREO_CULLING_BASE is the published alpha-reference recipe absolute path.
# XR_STEREO_CULLING_GROUPS=1 qualifies first split; 4 includes reversed/neither-eye.
python
import os,json
from pathlib import Path
base=Path(os.environ['XR_STEREO_CULLING_BASE'])
output=Path(os.environ['XR_STEREO_ALPHA_OUTPUT']);output.mkdir(parents=True,exist_ok=True)
groups=int(os.environ.get('XR_STEREO_CULLING_GROUPS','1'));assert groups in (1,4)
source=base.read_text()
def replace_once(old,new):
 global source
 assert source.count(old)==1,'base recipe seam changed: '+old[:60]
 source=source.replace(old,new)
custom=r"""
import math
CULL_GROUPS=int(os.environ.get('XR_STEREO_CULLING_GROUPS','1'))
CULL_PHASES=CULL_GROUPS*8
cull_records={}
querying=False
alias_records={}
def phase_info(phase):
 return dict(mode=phase//8,group=phase//8,eye=(phase//4)%2,layer='E' if phase%4>=2 else 'B',repeat=phase%2)
def upper_view(which,eye):
 return (eye==which) if which<2 else which==2
def fixture_expected_culled(which,j):
 return (which==2 and j==1) or (which==3 and j==0)
def phase_inputs():
 phase=iv('$phase')
 if not iv('$armed') or not 0<=phase<CULL_PHASES or not notifications_expired():return None
 info=phase_info(phase);enabled=info['layer']=='E'
 if iv('fixture_alpha_static_count')!=2 or iv('r_oit.value')!=1 or fv('r_wateralpha.value')!=0:return None
 if iv('vr_mirror.value')!=info['eye']+1 or iv('stereo_liquid_categories_valid') or iv('stereo_alpha_exceptional'):return None
 for eye in range(2):
  upper=upper_view(info['group'],eye)
  if fv(frame_view(eye,'up'))!=(1 if upper else 0) or fv(frame_view(eye,'down'))!=(0 if upper else -1):return None
 for j in range(2):
  if iv('cl.static_entities[fixture_alpha_static_indices[%d]]->alpha'%j)!=(128 if enabled else 1):return None
 return dict(phase=phase,**info,water=0.,alpha=128 if enabled else 1,wet_mask=0,exceptional=False,cl_time=fv('cl.time'))
def inject():
 global injected
 which=phase_info(iv('$phase'))['group']
 position=[0,1.6,0];rotation=[[0,-1,0],[1,0,0],[0,0,1]]
 write('openxr_frame.devices[0].matrix',[a for j in range(3) for a in rotation[j]+[position[j]]])
 for eye in range(2):
  eye_position=[0,1.6+(.032 if eye else -.032),0]
  write(frame_view(eye,'matrix'),[a for j in range(3) for a in rotation[j]+[eye_position[j]]])
  write(submitted_view(eye,'pose'),[0,0,2**-.5,2**-.5]+eye_position)
  upper=upper_view(which,eye)
  tangents=dict(left=-1.,right=1.,up=1. if upper else 0.,down=0. if upper else -1.)
  for field,value in tangents.items():write(frame_view(eye,field),[value])
  write(submitted_view(eye,'fov'),[math.atan(tangents[field]) for field in ('left','right','up','down')])
 if not injected:
  gdb.execute('call (void)V_ResetTrackedAim()')
  gdb.execute('call (void)R_InvalidateStereoReference()')
  injected=True
def fixture_index(ptr):
 for j in range(2):
  if ptr==iv('cl.static_entities[fixture_alpha_static_indices[%d]]'%j):return j
 return None
class CullReturn(gdb.FinishBreakpoint):
 def __init__(self,j):
  super().__init__(gdb.newest_frame(),internal=True);self.silent=True;self.j=j;self.phase=iv('$phase')
 def stop(self):
  if phase_inputs() is not None and iv('$phase')==self.phase:
   result=int(self.return_value);expected=fixture_expected_culled(phase_info(self.phase)['group'],self.j)
   assert bool(result)==expected,'native either-eye box result mismatch'
   values=cull_records.setdefault(self.phase,{});values[self.j]=values.get(self.j,0)+1
  return False
class CullEntry(gdb.Breakpoint):
 def stop(self):
  if not querying and phase_inputs() is not None:
   j=fixture_index(iv('e'))
   if j is not None:CullReturn(j)
  return False
class CullingAliasReturn(gdb.FinishBreakpoint):
 def __init__(self,j):
  super().__init__(gdb.newest_frame(),internal=True);self.silent=True;self.j=j;self.phase=iv('$phase');self.ptr=iv('aliaspolys')
  self.before=int(gdb.Value(self.ptr).cast(gdb.lookup_type('int').pointer()).dereference())
 def stop(self):
  if phase_inputs() is not None and iv('$phase')==self.phase:
   delta=int(gdb.Value(self.ptr).cast(gdb.lookup_type('int').pointer()).dereference())-self.before
   assert delta==(0 if fixture_expected_culled(phase_info(self.phase)['group'],self.j) else 4),'native alias acceptance mismatch'
   values=alias_records.setdefault(self.phase,{});values[self.j]=values.get(self.j,0)+1
  return False
class CullingAliasEntry(gdb.Breakpoint):
 def stop(self):
  inputs=phase_inputs()
  if inputs is not None and inputs['alpha']==128:
   j=fixture_index(iv('e'))
   if j is not None:CullingAliasReturn(j)
  return False
def observe():
 global querying
 phase=iv('$phase');info=phase_info(phase);eye=info['eye'];enabled=info['layer']=='E'
 assert phase_inputs() is not None and iv('cl.paused')
 assert iv('vulkan_globals.sample_count')==4 and iv('r_ssao.value')==1
 assert not iv('vulkan_globals.openxr_fragment_shading_rate_active') and not iv('vulkan_globals.openxr_fragment_density_map_active')
 identities=[]
 for j in range(2):
  ent='cl.static_entities[fixture_alpha_static_indices[%d]]'%j
  assert iv(ent+'->alpha')==(128 if enabled else 1)
  gdb.execute('set scheduler-locking on')
  querying=True
  try:
   gdb.execute('set $native_cull=(int)R_CullModelForEntity(%s)'%ent)
   gdb.execute('set $native_leaf=(mleaf_t*)Mod_PointInLeaf(%s->origin,cl.worldmodel)'%ent)
  finally:
   querying=False
   gdb.execute('set scheduler-locking off')
  culled=iv('$native_cull');assert bool(culled)==fixture_expected_culled(info['group'],j)
  contents=iv('$native_leaf->contents');assert contents==(-3 if j==0 else -1)
  if enabled and not culled:assert alias_records.get(phase,{}).get(j,0)>0 and cull_records.get(phase,{}).get(j,0)>0
  identities.append(dict(name='wet' if j==0 else 'dry',index=iv('fixture_alpha_static_indices[%d]'%j),contents=contents,native_culled=culled,in_path_cull_calls=cull_records.get(phase,{}).get(j,0),alias_calls=alias_records.get(phase,{}).get(j,0),origin=[fv(ent+'->origin[%d]'%i) for i in range(3)],model_mins=[fv(ent+'->model->mins[%d]'%i) for i in range(3)],model_maxs=[fv(ent+'->model->maxs[%d]'%i) for i in range(3)]))
 windows=subprocess.run(['xprop','-root','_NET_CLIENT_LIST'],capture_output=True,text=True,check=True,timeout=5).stdout
 owned=[]
 for wid in re.findall(r'0x[0-9a-f]+',windows):
  owner=subprocess.run(['xprop','-id',wid,'_NET_WM_PID'],capture_output=True,text=True,check=True,timeout=5).stdout
  if re.search(r'=\s*%d\s*$'%gdb.selected_inferior().pid,owner.strip()):owned.append(wid)
 assert len(owned)==1
 name='group%d-eye%d-%s-repeat%d.png'%(info['group'],eye,info['layer'],info['repeat'])
 subprocess.run(['import','-window',owned[0],str(root/name)],check=True,timeout=10)
 matrices=dict(center_clip=[fv('vulkan_globals.view_projection_matrix[%d]'%i) for i in range(16)],eye_clip=[[fv('vulkan_globals.stereo_clip_from_center[%d][%d]'%(e,i)) for i in range(16)] for e in range(2)])
 fovs=[{field:fv(frame_view(e,field)) for field in ('left','right','up','down')} for e in range(2)]
 planes=[dict(normal=[fv('frustum[%d].normal[%d]'%(j,i)) for i in range(3)],dist=fv('frustum[%d].dist'%j)) for j in range(4)]
 time=fv('cl.time')
 if samples:assert time==samples[0]['cl_time']
 samples.append(dict(phase=phase,**info,image=name,cl_time=time,native_extent=[iv('vid.width'),iv('vid.height')],mirror_extent=[iv('openxr_mirror_extent.width'),iv('openxr_mirror_extent.height')],msaa=iv('vulkan_globals.sample_count'),ssao=iv('r_ssao.value'),oit=iv('r_oit.value'),head_valid=iv('openxr_frame.devices[0].valid'),head_tracked=iv('openxr_frame.devices[0].tracked'),matrices=matrices,fovs=fovs,planes=planes,identities=identities,presentations=list(presentations[phase].values()),notify_settled_frames=notify_settled[phase]))
 (root/'culling.json').write_text(json.dumps(dict(status='passed' if phase==CULL_PHASES-1 else 'running',samples=samples),indent=2)+'\n')
 print('STEREO_CULLING_CAPTURE',phase,name,flush=True)
"""
replace_once('end\nbreak VRXR_StereoClip',custom+'\nend\nbreak VRXR_StereoClip')
replace_once("startup_text='pause\\nfixture_alpha_opacity 0\\nr_wateralpha 0\\nvr_mirror %d\\n'", "startup_text='r_oit 1\\npause\\nfixture_alpha_opacity 0\\nr_wateralpha 0\\nvr_mirror %d\\n'")
replace_once("phase=iv('$phase');layer=(phase//2)%4\ncommand='fixture_alpha_opacity %d\\nr_wateralpha %g\\nvr_mirror %d\\n'%(int(layer in (1,3)),.5 if layer in (2,3) else 0,target_eye+1)", "phase=iv('$phase');info=phase_info(phase)\ncommand='fixture_alpha_opacity %d\\nr_wateralpha 0\\nvr_mirror %d\\n'%(int(info['layer']=='E'),info['eye']+1)")
replace_once("alias_observer=AliasConsumer('*R_DrawAliasModel',internal=True)\nalias_observer.silent=True", "cull_observer=CullEntry('*R_CullModelForEntity',internal=True)\ncull_observer.silent=True\nalias_observer=CullingAliasEntry('*R_DrawAliasModel',internal=True)\nalias_observer.silent=True")
replace_once('$phase==16','$phase==%d'%(groups*8))
replace_once("iv('$phase')!=16","iv('$phase')!=%d"%(groups*8))
source=source.replace('ALPHA_LAYER_REFERENCE_NATIVE_PASSED','STEREO_CULLING_NATIVE_PASSED')
generated=output/'culling-expanded.gdb';generated.write_text(source)
assert not any(c.isspace() for c in str(generated)), 'use a private output path without whitespace'
gdb.execute('source '+str(generated))
end
