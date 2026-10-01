set pagination off
set confirm off
set print thread-events off
set debuginfod enabled off
set $loads=0
set $rendered=0
set $phases=0
break Mod_CalcSurfaceExtentsTask
commands
 silent
 if surfnum == 0 && $_streq((*mod_ptr)->name, "maps/mj4m1.bsp")
  if 'tasks.c'::is_worker != 1
   quit 1
  end
  set $loads=$loads+1
  set $rendered=0
  printf "LARGE_MAP_NATIVE_WORKER_OBSERVED load=%d\n", $loads
  disable 1
 end
 continue
end
break GL_EndXRFrame
commands
 silent
 if cls.signon == 4 && sv.active && !cls.demoplayback && $_streq(sv.name, "mj4m1")
  set $rendered=$rendered+1
  if $rendered == 12
   set $phases=$phases+1
   if $loads != $phases || vulkan_globals.stereo_active
    quit 1
   end
   set $surfaces=(int)Fixture_VerifyWorldExtents("maps/mj4m1.bsp")
   printf "LARGE_MAP_REPLACEMENT_PHASE_PASSED phase=%d surfaces=%d\n", $phases, $surfaces
   if $phases == 1
    set $first_surfaces=$surfaces
    enable 1
    call (void)Cbuf_AddText("screenshot png\nwait\nwait\nmap mj4m1\n")
   else
    if $surfaces != $first_surfaces || $loads != 2
     quit 1
    end
    python
import gdb,json,os
from pathlib import Path
Path(os.environ['QSVR_EXTENTS_RESULT']).write_text(json.dumps({'passed':True,'loads':int(gdb.parse_and_eval('$loads')),'phases':int(gdb.parse_and_eval('$phases')),'settled_frames_each':12,'surfaces_each':int(gdb.parse_and_eval('$surfaces'))},indent=2)+'\n')
    end
    printf "LARGE_MAP_EXTENTS_NATIVE_PASSED repeated native load, worker and serial extents\n"
    call (void)Cbuf_AddText("screenshot png\nwait\nwait\nquit\n")
    disable 2
   end
  end
 end
 continue
end
run
