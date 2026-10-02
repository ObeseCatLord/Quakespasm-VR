# Controlled located poses; actual native Vulkan/Monado rendering. See README.
set pagination off
set confirm off
set print thread-events off
set debuginfod enabled off
set $started=0
set $startup=0
set $phase=0
set $frames=0
python
import gdb,json,struct,os,re,subprocess
from collections import Counter
from pathlib import Path
root=Path(os.environ['XR_STEREO_LIQUID_OUTPUT'])
root.mkdir(exist_ok=True)
def iv(s):return int(gdb.parse_and_eval(s))
def write(s,values):gdb.selected_inferior().write_memory(int(gdb.parse_and_eval('&('+s+')')),struct.pack('='+str(len(values))+'f',*values))
injected=False
alpha_calls=Counter()
samples=[]
def inject():
 global injected
 # A rigid 90-degree roll places the left eye below this actual water plane.
 s=1 if iv('$phase')<2 else -1
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
 phase=iv('$phase');wet=1 if phase<2 else 2
 item=dict(phase=phase,head_valid=iv('openxr_frame.devices[0].valid'),head_tracked=iv('openxr_frame.devices[0].tracked'),
 basis_valid=iv('stereo_tracking_basis_valid'),categories_valid=iv('stereo_liquid_categories_valid'),
 mask=iv('stereo_wet_eye_mask'),exceptional=iv('stereo_alpha_exceptional'),oit=iv('r_oit.value'),
 msaa=iv('vulkan_globals.sample_count'),ssao=iv('r_ssao.value'),alpha_under=iv('cl_numvisedicts_alpha_underwater'),alpha_over=iv('cl_numvisedicts_alpha_overwater'),contents=[],alpha_calls=[list(key)+[count] for key,count in sorted(alpha_calls.items())])
 for eye in range(2):
  gdb.execute('set scheduler-locking on')
  gdb.execute('set $leaf=(mleaf_t*)Mod_PointInLeaf(r_stereo_origins[%d],cl.worldmodel)'%eye)
  gdb.execute('set scheduler-locking off')
  item['contents'].append(iv('$leaf->contents'))
 assert item['contents']==([-3,-1] if wet==1 else [-1,-3]),item
 assert item['head_valid']==1 and item['head_tracked']==0 and item['basis_valid']==0,item
 if phase<4:
  assert item['categories_valid'] and item['mask']==wet and item['exceptional'],item
  assert item['oit']==0,item
  assert set(alpha_calls)=={(0,1,wet),(0,2,3^wet),(1,1,3^wet),(1,2,wet)},item
 else:
  assert item['oit']==1 and not item['categories_valid'] and not item['mask'] and not item['exceptional'] and not alpha_calls,item
 assert item['msaa']==4 and item['ssao']==1,item
 windows=subprocess.run(['xprop','-root','_NET_CLIENT_LIST'],capture_output=True,text=True,check=True,timeout=5).stdout
 candidates=[]
 for wid in re.findall(r'0x[0-9a-f]+',windows):
  owner=subprocess.run(['xprop','-id',wid,'_NET_WM_PID'],capture_output=True,text=True,check=True,timeout=5).stdout
  if re.search(r'=\s*%d\s*$'%gdb.selected_inferior().pid,owner.strip()):candidates.append(wid)
 assert len(candidates)==1,'owned mirror window not unique'
 subprocess.run(['import','-window',candidates[0],str(root/('reset-phase%d-mirror.png'%phase))],check=True,timeout=10)
 samples.append(item);alpha_calls.clear()
 (root/'result-reset.json').write_text(json.dumps(dict(status='passed' if phase==4 else 'running',samples=samples),indent=2)+'\n')

end
break VRXR_StereoClip if $started && cls.signon == 4 && openxr_frame.should_render
commands 1
 silent
 python inject()
 continue
end
break GL_EndXRFrame
commands 2
 silent
 if !$started && vulkan_globals.stereo_active
  set $startup=$startup+1
  if $startup == 8
   call (void)Cbuf_AddText("map e1m1\n")
   set $started=1
  end
 else
  if cls.signon == 4 && vulkan_globals.stereo_active
   set $frames=$frames+1
   if $frames == 4
    call (void)Cbuf_AddText("god\nsetpos 836 832 -322 0 90 0\n")
   end
   if $frames == 20
    python observe()
    set $frames=8
    set $phase=$phase+1
    if $phase == 1 || $phase == 3
     call (void)Cbuf_AddText("vr_mirror 2\n")
    end
    if $phase == 2
     call (void)Cbuf_AddText("vr_mirror 1\n")
    end
    if $phase == 4
     call (void)Cbuf_AddText("r_oit 1\n")
    end
    if $phase == 5
     printf "STEREO_LIQUID_NATIVE_VALID_UNTRACKED_PASSED\n"
     call (void)Cbuf_AddText("quit\n")
     disable 1 2 5
    end
   end
  end
 end
 continue
end
break Host_Error
break Sys_Error
break R_DrawStereoAlphaListAtStage if $started && cls.signon == 4 && stereo_alpha_exceptional
commands 5
 silent
 python alpha_calls[(iv('stage'),iv('alphapass'),iv('eye_mask'))]+=1
 continue
end
run

python
if gdb.selected_inferior().pid or iv("$phase")!=5 or iv("$_exitcode")!=0:
 gdb.execute("quit 1")
end
quit 0
