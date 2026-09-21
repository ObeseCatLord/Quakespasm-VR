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
import gdb, os, subprocess, math
def value(expr):
    return int(gdb.parse_and_eval(expr))
i = value("$probes")
expected = (i // 4, 4 if (i // 2) % 2 else 1, 1 - i % 2, 1) if i < 12 else (2, 4, 1, 0)
actual = tuple(value(expr) for expr in ("frame_oit_mode", "vulkan_globals.sample_count", "indirect", "vid_palettize.value"))
print("XR_SMOKE_probe=%d effective_oit=%d samples=%d indirect=%d palette=%d" % ((i,) + actual), flush=True)
print("XR_SMOKE_frame=%d requested_indirect=%d should_render=%d paused=%d" % tuple(value(expr) for expr in ("r_framecount", "r_indirect.value", "openxr_frame.should_render", "cl.paused")), flush=True)
if i > 18 or actual != expected or value("vid.restart_next_frame"):
    print("XR_SMOKE_FAILED: effective mode mismatch, expected %s" % (expected,), flush=True)
    gdb.execute("quit 1")
capture = os.environ.get("XR_SMOKE_CAPTURE")
if i >= 12:
    units = (2 if i == 17 else 1.5 if i in (13, 14) else 1.0) / (1.5 * .0254)
    floor = -8 if i == 14 else -16
    head_height = float(gdb.parse_and_eval("openxr_frame.devices[0].matrix[1][3]"))
    player_z = float(gdb.parse_and_eval("cl.entities[cl.viewentity].origin[2]"))
    center_z = float(gdb.parse_and_eval("r_stereo_origins[0][2] - vulkan_globals.stereo_eye_offset[0][2]"))
    radius = float(gdb.parse_and_eval("r_stereo_radius"))
    eye_distance = math.sqrt(sum(float(gdb.parse_and_eval("openxr_frame.views[0].matrix[%d][3] - openxr_frame.devices[0].matrix[%d][3]" % (j, j))) ** 2 for j in range(3)))
    expected_z = player_z + 1/32 + head_height * units + floor
    if i == 17:
        expected_z = float(gdb.parse_and_eval("stereo_base_origin[2]")) + (head_height - float(gdb.parse_and_eval("stereo_reference_position[1]"))) * units
    print("XR_SMOKE_view=%d height=%.4f expected=%.4f radius=%.4f" % (i, center_z, expected_z, radius), flush=True)
    if not value("openxr_frame.floor_referenced") or value("base_player_view") != (i != 17) or abs(center_z - expected_z) > .003 or abs(radius - eye_distance * units) > .003:
        print("XR_SMOKE_FAILED: live floor/scale mismatch (this qualification requires a floor-referenced runtime)", flush=True)
        gdb.execute("quit 1")
if capture and i in (0, 12, 13, 14, 17):
    stem, ext = os.path.splitext(capture)
    suffix = {0: "-initial", 12: "-resized", 13: "-scaled", 14: "-raised", 17: "-chase"}[i]
    subprocess.run(["import", "-window", "Monado", stem + suffix + ext], check=True)
 end
 set $probes = $probes + 1
 continue
end
break *Host_Quit_f
commands 4
 silent
 printf "XR_SMOKE_final target=%dx%d window=%dx%d\n", vid.width, vid.height, openxr_desktop_width, openxr_desktop_height
 if !$signed_on || !$tasks_seen || $probes != 19 || vid.width != openxr_frame.views[0].width || vid.height != openxr_frame.views[0].height || openxr_desktop_width != 800 || openxr_desktop_height != 600
  printf "XR_SMOKE_FAILED: signon/tasks/mode/resize gate did not complete\n"
  quit 1
 end
 continue
end
run
