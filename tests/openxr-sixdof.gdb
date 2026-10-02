# Controlled tracking; real native Vulkan/Monado images. See README.
set pagination off
set confirm off
set print thread-events off
set debuginfod enabled off
set $phase=0
set $frames=0
set $startup_frames=0
set $started=0
python
import gdb, json, math, os, re, struct, subprocess
from pathlib import Path
root=Path(os.environ['XR_SIXDOF_OUTPUT'])
root.mkdir(exist_ok=True)
phases=[('baseline',(0,0,0),None,0),('right',(.35,0,0),None,0),
 ('left',(-.35,0,0),None,0),('up',(0,.25,0),None,0),
 ('forward',(0,0,-.35),None,0),('yaw',(0,0,0),1,20),
 ('pitch',(0,0,0),0,15),('roll',(0,0,0),2,15)]
samples=[]
initial_head=None
initial_eyes=None
initial_eye_quaternions=None
baseline=None
last=None
last_observation=None

def val(s): return float(gdb.parse_and_eval(s))
def iv(s): return int(gdb.parse_and_eval(s))
def vec(s): return [val('%s[%d]'%(s,j)) for j in range(3)]
def check(ok,message):
 if not ok: raise RuntimeError(message)
def write(s,values):
 address=int(gdb.parse_and_eval('&('+s+')'))
 gdb.selected_inferior().write_memory(address,struct.pack('='+str(len(values))+'f',*values))
def fail(exc):
 (root/'result.json').write_text(json.dumps(dict(status='failed',failure=str(exc),samples=samples,last_observation=last_observation),indent=2)+'\n')
 print('SIXDOF_GPU_FAILED: '+str(exc),flush=True)
 gdb.execute('quit 1')

def inject():
 global initial_head,initial_eyes,initial_eye_quaternions,last
 phase=iv('$phase')
 if phase>=len(phases): return
 check(iv('cl.protocol_qsvr')==0,'requires public camera-only admission')
 check(not iv('cls.demoplayback') and not iv('cl.intermission'),'requires live gameplay, not demo/intermission')
 if initial_head is None:
  initial_head=[val('openxr_frame.devices[0].matrix[%d][3]'%j) for j in range(3)]
  raw_rotation=[[val('openxr_frame.devices[0].matrix[%d][%d]'%(j,k)) for k in range(3)] for j in range(3)]
  initial_eyes=[]
  initial_eye_quaternions=[]
  for eye in range(2):
   offset=[val('openxr_frame.views[%d].matrix[%d][3]'%(eye,j))-initial_head[j] for j in range(3)]
   initial_eyes.append([sum(raw_rotation[j][k]*offset[j] for j in range(3)) for k in range(3)])
   relative=[[sum(raw_rotation[j][k]*val('openxr_frame.views[%d].matrix[%d][%d]'%(eye,j,c)) for j in range(3)) for c in range(3)] for k in range(3)]
   trace=sum(relative[j][j] for j in range(3))
   check(trace>0,'simulated relative eye rotation needs positive trace')
   qw=math.sqrt(1+trace)/2
   initial_eye_quaternions.append([(relative[2][1]-relative[1][2])/(4*qw),
    (relative[0][2]-relative[2][0])/(4*qw),(relative[1][0]-relative[0][1])/(4*qw),qw])
  check(all(all(math.isfinite(a) and abs(a)<.1 for a in v) for v in initial_eyes),'invalid simulated eye offsets')
  check(initial_eyes[0][0]<0<initial_eyes[1][0],'invalid simulated IPD')
  gdb.execute('call (void)V_ResetTrackedAim()')
  gdb.execute('call (void)R_InvalidateStereoReference()')
 label,delta,axis,degrees=phases[phase]
 angle=math.radians(degrees)
 q=[0.0,0.0,0.0,math.cos(angle/2)]
 if axis is not None: q[axis]=math.sin(angle/2)
 x,y,z,w=q
 rotation=[[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],
  [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],
  [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]]
 position=[initial_head[j]+delta[j] for j in range(3)]
 write('openxr_frame.devices[0].matrix',[a for j in range(3) for a in rotation[j]+[position[j]]])
 for eye in range(2):
  offset=[sum(rotation[j][k]*initial_eyes[eye][k] for k in range(3)) for j in range(3)]
  eye_position=[position[j]+offset[j] for j in range(3)]
  ex,ey,ez,ew=initial_eye_quaternions[eye]
  eye_q=[w*ex+x*ew+y*ez-z*ey,w*ey-x*ez+y*ew+z*ex,
   w*ez+x*ey-y*ex+z*ew,w*ew-x*ex-y*ey-z*ez]
  ax,ay,az,aw=eye_q
  eye_rotation=[[1-2*(ay*ay+az*az),2*(ax*ay-az*aw),2*(ax*az+ay*aw)],
   [2*(ax*ay+az*aw),1-2*(ax*ax+az*az),2*(ay*az-ax*aw)],
   [2*(ax*az-ay*aw),2*(ay*az+ax*aw),1-2*(ax*ax+ay*ay)]]
  write('openxr_frame.views[%d].matrix'%eye,[a for j in range(3) for a in eye_rotation[j]+[eye_position[j]]])
  # Match submitted XR projection poses to the same controlled located eye pose.
  write("'(anonymous namespace)::g'.views[%d].pose"%eye,eye_q+eye_position)
 last=(label,delta,rotation)

def observe():
 global baseline,last_observation
 phase=iv('$phase')
 label,delta,rotation=last
 units=1/(1.5*.0254)
 item=dict(label=label,center=[val('r_stereo_origins[0][%d]-vulkan_globals.stereo_eye_offset[0][%d]'%(j,j)) for j in range(3)],
  forward=vec('stereo_forward'),right=vec('stereo_right'),up=vec('stereo_up'),
  eyes=[vec('r_stereo_origins[%d]'%eye) for eye in range(2)],samples=iv('vulkan_globals.sample_count'),ssao=iv('r_ssao.value'))
 item['aim_mode']=val('vr_aimmode.value')
 item['base_angles_valid']=iv('base_angles_valid')
 item['tracked_aim_ready']=iv('tracked_aim_ready')
 item['viewangles']=vec('cl.viewangles')
 item['tracked_view']=vec('tracked_view_angles')
 item['base_aim']=vec('base_aim_angles')
 item['reference']=[val('stereo_reference_position[%d]'%j) for j in range(3)]
 item['tracking_yaw']=val('tracked_yaw')
 item['raw_angles']=vec('tracked_raw_angles')
 item['head_translation']=[val('openxr_frame.devices[0].matrix[%d][3]'%j) for j in range(3)]
 last_observation=item
 check(item['tracked_aim_ready'] and item['aim_mode']==7,'normal tracked controller camera not ready')
 check(item['samples']==4 and item['ssao']==1,'graphics quality changed')
 check(not iv('vulkan_globals.openxr_fragment_shading_rate_active'),'foveation must be off')
 if baseline is None: baseline=item
 f,r,u=(baseline[k] for k in ('forward','right','up'))
 def world(v): return [r[j]*v[0]+u[j]*v[1]-f[j]*v[2] for j in range(3)]
 expected_center=[baseline['center'][j]+world(delta)[j]*units for j in range(3)]
 expected_basis={name:world([sign*rotation[j][column] for j in range(3)])
  for name,column,sign in [('forward',2,-1),('right',0,1),('up',1,1)]}
 for j in range(3): check(abs(item['center'][j]-expected_center[j])<.01,'center '+label)
 for name in expected_basis:
  check(max(abs(a-b) for a,b in zip(item[name],expected_basis[name]))<.0005,'basis '+name+' '+label)
 for eye in range(2):
  local=initial_eyes[eye]
  offset=[expected_basis['right'][j]*local[0]+expected_basis['up'][j]*local[1]-expected_basis['forward'][j]*local[2] for j in range(3)]
  expected_eye=[expected_center[j]+offset[j]*units for j in range(3)]
  check(max(abs(a-b) for a,b in zip(item['eyes'][eye],expected_eye))<.01,'eye '+str(eye)+' '+label)
 samples.append(item)
 if os.environ.get('XR_SIXDOF_NULL_COMPOSITOR') != '1':
  subprocess.run(['import','-window','Monado',str(root/(label+'.png'))],check=True,timeout=10)
 check(iv('openxr_mirror_ready') and iv('vr_mirror.value')==1,'native left-eye mirror unavailable')
 windows=subprocess.run(['xprop','-root','_NET_CLIENT_LIST'],capture_output=True,text=True,check=True,timeout=5).stdout
 candidates=[]
 for wid in re.findall(r'0x[0-9a-f]+',windows):
  owner=subprocess.run(['xprop','-id',wid,'_NET_WM_PID'],capture_output=True,text=True,check=True,timeout=5).stdout
  if re.search(r'=\s*%d\s*$'%gdb.selected_inferior().pid,owner.strip()): candidates.append(wid)
 check(len(candidates)==1,'owned mirror window not unique')
 subprocess.run(['import','-window',candidates[0],str(root/(label+'-mirror.png'))],check=True,timeout=10)
 (root/'result.json').write_text(json.dumps(dict(status='passed' if phase==len(phases)-1 else 'running',tracking='controlled injection after runtime location',samples=samples),indent=2)+'\n')
end
# Successful location, before production eye projection and camera preparation.
break VRXR_StereoClip if $started && cls.signon == 4 && openxr_frame.should_render
commands 1
 silent
 python
try: inject()
except Exception as exc: fail(exc)
 end
 continue
end
break *GL_EndXRFrame
commands 2
 silent
 if !$started && vulkan_globals.stereo_active
  set $startup_frames=$startup_frames+1
  if $startup_frames >= 8
   python
try:
 check(not iv('cls.demoplayback') and iv('key_dest')==iv('key_menu'),'default VR startup must be menu without demo')
 if os.environ.get('XR_SIXDOF_NULL_COMPOSITOR') != '1':
  subprocess.run(['import','-window','Monado',str(root/'startup-menu.png')],check=True,timeout=10)
except Exception as exc: fail(exc)
   end
   printf "SIXDOF_GPU_STARTUP_MENU_NO_DEMO_PASSED\n"
   call (void)Cbuf_AddText("vr_mirror 1\nsv_qsvr_private 0\nvr_foveation 0\nvr_world_scale 1\nvr_floor_offset -16\nvr_aimmode 7\nvid_fsaa 4\nr_ssao 1\nmap e1m1\n")
   set $started=1
  end
 else
 if cls.signon == 4 && vulkan_globals.stereo_active
  set $frames=$frames+1
  if $frames >= 8
   python
try: observe()
except Exception as exc: fail(exc)
   end
   set $frames=0
   set $phase=$phase+1
   if $phase == 8
    printf "SIXDOF_GPU_NATIVE_PASSED\n"
    call (void)Cbuf_AddText("quit\n")
    disable 1 2
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
if gdb.selected_inferior().pid or iv('$phase')!=8:
 if gdb.selected_inferior().pid: gdb.execute('bt 12')
 gdb.execute('quit 1')
end
quit 0
