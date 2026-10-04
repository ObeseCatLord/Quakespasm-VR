# Run in a private licensed profile; optional isolated simulated Monado for -openxr.
# Exercises live renderer state and fallback, not performance or full pixel equivalence.
set pagination off
set confirm off
set print thread-events off
set debuginfod enabled off
python
import gdb,os
frame=0
case=-1
checks=set()
cases=[('native',0,1,1,0,1,0,0,0),('cluster-left',1,1,1,0,1,0,0,0),('cluster-right',1,1,1,0,1,0,0,0),('dither-low',1,1,1,0,1,.25,0,0),('dither-high-oit',1,1,1,0,1,1,2,1),('tasks-off',1,1,1,0,0,.5,1,0),('cpu-fallback',1,1,0,0,1,0,0,0),('dynamics-off',1,0,1,0,1,0,0,0),('shadow-fallback',1,1,1,1,1,0,0,0),('native-return',0,1,1,0,1,0,0,0)]
def v(e):return int(gdb.parse_and_eval(e))
def call(e):gdb.execute('call (void)'+e,to_string=True)
def setvar(n,x):call('Cvar_SetValueQuick(&%s,%s)'%(n,x))
def main_probe():
 global frame,case
 if v('cls.signon')!=4:return
 if v('vulkan_globals.stereo_active') and not v('openxr_frame.should_render'):return
 frame+=1
 gdb.execute('set scheduler-locking off',to_string=True)
 if frame<8:return
 nextcase=(frame-8)//12
 if nextcase>=len(cases):
  assert len(checks)==len(cases),checks
  print('SELECTED_LIGHTING_NATIVE_PASSED stereo=%d cases=%d'%(v('vulkan_globals.stereo_active'),len(checks)),flush=True)
  call('Cbuf_AddText("quit\\n")');gdb.execute('disable 1',to_string=True);return
 if nextcase!=case:
  case=nextcase
  label,cluster,dynamic,gpu,shadow,tasks,dither,oit,palette=cases[case]
  for n,x in [('r_clustered_lights',cluster),('r_dynamic',dynamic),('r_gpulightmapupdate',gpu),('r_rtshadows',shadow),('r_tasks',tasks),('r_surface_dither',dither),('r_oit',oit),('vid_palettize',palette)]:setvar(n,x)
  if v('vulkan_globals.stereo_active'):setvar('vr_mirror',2 if label=='cluster-right' else 1)
  print('LIGHTING_CASE_BEGIN '+label,flush=True)
 # Use engine-owned lights and keep a stable ordinary and KEX/cone witness alive.
 for i in range(2):
  for a in range(3):gdb.execute('set cl_dlights[%d].origin[%d]=r_refdef.vieworg[%d]+vpn[%d]*120+%d'%(i,a,a,a,(-30 if i==0 else 30) if a==1 else 0),to_string=True)
  for field,val in [('radius',300),('die',1e8),('decay',0),('minlight',0),('key',900+i),('cone_cos',-1),('kex_intensity',1 if i else 0)]:gdb.execute('set cl_dlights[%d].%s=%s'%(i,field,val),to_string=True)
  for a in range(3):gdb.execute('set cl_dlights[%d].color[%d]=%s'%(i,a,1 if a==i else .2),to_string=True)
 if (frame-8)%12==9:call('Cbuf_AddText("screenshot png\\n")')
def end_probe():
 if case<0 or case in checks or (frame-8)%12<5:return
 label,cluster,dynamic,gpu,shadow,tasks,dither,oit,palette=cases[case]
 active=bool(cluster and dynamic and gpu and not shadow)
 assert v('clustered_lighting_effective')==active,(label,v('clustered_lighting_effective'))
 assert bool(v('use_tasks'))==bool(tasks and gpu),(label,v('use_tasks'))
 assert v('frame_oit_mode')==oit,(label,v('frame_oit_mode'))
 data=gdb.parse_and_eval('*(cluster_lighting_frame_t *)(cluster_lighting_buffer_mapped+bmodel_instances_index*cluster_lighting_slot_stride)')
 assert int(data['counts'][1])==(2 if v('vulkan_globals.stereo_active') else 1)
 assert float(data['params'][3])==float(active)
 assert float(data['params'][2])==dither
 assert int(data['counts'][0])>=2 if active else int(data['counts'][0])==0
 print('LIGHTING_CASE_PASSED '+label+' samples=%d lights=%d eyes=%d'%(v('vulkan_globals.sample_count'),int(data['counts'][0]),int(data['counts'][1])),flush=True)
 checks.add(case)
end
break SCR_UpdateScreen
commands
 silent
 python main_probe()
 continue
end
break GL_EndRendering
commands
 silent
 python end_probe()
 continue
end
break IN_Activate
commands
 silent
 return
 continue
end
break Sys_Error
commands
 silent
 bt 8
 quit 1
end
break Host_Error
commands
 silent
 bt 8
 quit 1
end
run
