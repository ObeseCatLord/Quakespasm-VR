# Native alpha layer capture; actual Vulkan/Monado rendering. See tests/README.md.
# Requires the native test binary linked with tests/stereo_alpha_native_fixture.c,
# fixtures emitted by tests/prepare_stereo_alpha_native.py, private alpha.cfg,
# licensed stock e1m1 data, and an isolated simulated Monado environment.
# Set XR_STEREO_ALPHA_OUTPUT to the writable capture output root.
# Controlled located poses; actual native Vulkan/Monado rendering. See README.
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
def iv(s):return int(gdb.parse_and_eval(s))
def write(s,values):gdb.selected_inferior().write_memory(int(gdb.parse_and_eval('&('+s+')')),struct.pack('='+str(len(values))+'f',*values))
injected=False
samples=[]
def inject():
 global injected
 # A rigid 90-degree roll places the left eye below this actual water plane.
 s=1 if iv('$phase')<8 else -1
 position=[0,1.6,0];rotation=[[0,-s,0],[s,0,0],[0,0,1]]
 write('openxr_frame.devices[0].matrix',[a for j in range(3) for a in rotation[j]+[position[j]]])
 for eye in range(2):
  eye_position=[0,1.6+s*(.032 if eye else -.032),0]
  write('openxr_frame.views[%d].matrix'%eye,[a for j in range(3) for a in rotation[j]+[eye_position[j]]])
  write("'(anonymous namespace)::g'.views[%d].pose"%eye,[0,0,s*2**-.5,2**-.5]+eye_position)
 if not injected:
  gdb.execute('call (void)V_ResetTrackedAim()')
  gdb.execute('call (void)R_InvalidateStereoReference()')
  injected=True
def observe():
 phase=iv('$phase');group=phase//8;eye=(phase//4)%2;layer=('B','E','W','C')[phase%4]
 enabled=layer in ('E','C');water=.5 if layer in ('W','C') else 0.0
 assert iv('fixture_alpha_static_count')==2 and iv('cl.num_statics')>=2
 assert iv('cl.paused') and iv('vulkan_globals.sample_count')==4 and iv('r_ssao.value')==1 and iv('r_oit.value')==0
 assert float(gdb.parse_and_eval('r_wateralpha.value'))==water and iv('vr_mirror.value')==eye+1
 assert iv('stereo_liquid_categories_valid') and iv('stereo_wet_eye_mask')==(1 if group==0 else 2)
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
 name='group%d-eye%d-%s.png'%(group,eye,layer)
 subprocess.run(['import','-window',owned[0],str(root/name)],check=True,timeout=10)
 time=float(gdb.parse_and_eval('cl.time'))
 if samples:assert time==samples[0]['cl_time']
 samples.append(dict(phase=phase,group=group,eye=eye,layer=layer,image=name,identities=identities,cl_time=time,formats=dict(scene=iv('vulkan_globals.color_format'),xr=iv('vulkan_globals.stereo_color_format'),mirror=iv('vulkan_globals.swap_chain_format')),wet_mask=iv('stereo_wet_eye_mask'),alpha_under=iv('cl_numvisedicts_alpha_underwater'),alpha_over=iv('cl_numvisedicts_alpha_overwater')))
 (root/'layers.json').write_text(json.dumps(dict(status='passed' if phase==15 else 'running',samples=samples),indent=2)+'\n')
 print('ALPHA_LAYER_CAPTURE',phase,name,flush=True)

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
     call (void)Cbuf_AddText("god\nsetpos 836 832 -322 0 90 0\nfixture_alpha_init\n")
    end
    if $frames == 20
     call (void)Cbuf_AddText("pause\nfixture_alpha_opacity 0\nr_wateralpha 0\nvr_mirror 1\n")
     set $frames=0
     set $armed=1
    end
   else
    if $frames == 8
     python observe()
     set $frames=0
     set $phase=$phase+1
     if $phase==16
      printf "ALPHA_LAYER_CAPTURE_NATIVE_PASSED\n"
      call (void)Cbuf_AddText("quit\n")
      disable 1 2
     else
      python
phase=iv('$phase');layer=phase%4;eye=(phase//4)%2
command='fixture_alpha_opacity %d\nr_wateralpha %g\nvr_mirror %d\n'%(int(layer in (1,3)),.5 if layer in (2,3) else 0,eye+1)
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
run
python
if gdb.selected_inferior().pid or iv('$phase')!=16 or iv('$_exitcode')!=0:gdb.execute('quit 1')
end
quit 0
