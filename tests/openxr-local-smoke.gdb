# Optional integration check: an isolated Monado simulated service must be live.
# See README.md. The process and normal command queue remain the engine owners.
set pagination off
set confirm off
set print thread-events off
set $signed_on = 0
set $tasks_seen = 0
set $probes = 0
break SCR_UpdateScreen if cls.signon == 4 && vulkan_globals.stereo_active && openxr_frame.should_render
commands 1
 silent
 printf "XR_SMOKE_signed_on target=%dx%d runtime=%ux%u\n", vid.width, vid.height, openxr_frame.views[0].width, openxr_frame.views[0].height
 if vid.width != openxr_frame.views[0].width || vid.height != openxr_frame.views[0].height
  printf "XR_SMOKE_FAILED: desktop window changed the XR target dimensions\n"
  quit 1
 end
 set $signed_on = 1
 printf "XR_SMOKE_task_settings requested=%d gpu_lightmaps=%d\n", (int)r_tasks.value, (int)r_gpulightmapupdate.value
 call (void) Cbuf_InsertText("exec openxr-local-smoke.cfg\n")
 disable 1
 enable 2
 continue
end
break GL_EndRendering
commands 2
 silent
 if !use_tasks
  printf "XR_SMOKE_FAILED: task rendering is not effective\n"
  quit 1
 end
 set $tasks_seen = 1
 disable 2
 continue
end
disable 2
# Entry addresses avoid multiple source-line locations in optimized functions.
break *CL_Viewpos_f
commands 3
 silent
 python
import gdb, os, subprocess
def value(expr):
    return int(gdb.parse_and_eval(expr))
i = value("$probes")
expected = (i // 4, 4 if (i // 2) % 2 else 1, 1 - i % 2, 1) if i < 12 else (2, 4, 1, 0)
actual = tuple(value(expr) for expr in ("frame_oit_mode", "vulkan_globals.sample_count", "indirect", "vid_palettize.value"))
print("XR_SMOKE_probe=%d effective_oit=%d samples=%d indirect=%d palette=%d" % ((i,) + actual), flush=True)
print("XR_SMOKE_frame=%d requested_indirect=%d should_render=%d paused=%d" % tuple(value(expr) for expr in ("r_framecount", "r_indirect.value", "openxr_frame.should_render", "cl.paused")), flush=True)
if i > 12 or actual != expected or value("vid.restart_next_frame"):
    print("XR_SMOKE_FAILED: effective mode mismatch, expected %s" % (expected,), flush=True)
    gdb.execute("quit 1")
capture = os.environ.get("XR_SMOKE_CAPTURE")
if capture and i in (0, 12):
    stem, ext = os.path.splitext(capture)
    subprocess.run(["import", "-window", "Monado", stem + ("-initial" if i == 0 else "-resized") + ext], check=True)
 end
 set $probes = $probes + 1
 continue
end
break *Host_Quit_f
commands 4
 silent
 printf "XR_SMOKE_final target=%dx%d window=%dx%d\n", vid.width, vid.height, openxr_desktop_width, openxr_desktop_height
 if !$signed_on || !$tasks_seen || $probes != 13 || vid.width != openxr_frame.views[0].width || vid.height != openxr_frame.views[0].height || openxr_desktop_width != 800 || openxr_desktop_height != 600
  printf "XR_SMOKE_FAILED: signon/tasks/mode/resize gate did not complete\n"
  quit 1
 end
 continue
end
run
