# Run only in a disposable licensed profile with the assertion-enabled engine.
# Use a private simulated Monado service for -openxr. No device/driver reset.
# This checks actual renderer ownership and restart paths; the GPU mip fixture
# separately compares depth/tag pixels against its independent oracle.
set pagination off
set confirm off
set print thread-events off
set debuginfod enabled off
python
import gdb

frame = 0
case = -1
checks = set()
cases = [(quality, half, samples) for samples in (1, 4)
         for half in (0, 1) for quality in (1, 2, 3)]

def value(expression):
    return int(gdb.parse_and_eval(expression))

def call(expression):
    gdb.execute('call (void)' + expression, to_string=True)

def begin_probe():
    global frame, case
    if value('cls.signon') != 4:
        return
    if value('vulkan_globals.stereo_active') and not value('openxr_frame.should_render'):
        return
    frame += 1
    gdb.execute('set scheduler-locking off', to_string=True)
    if frame < 8:
        return
    next_case = (frame - 8) // 12
    if next_case >= len(cases):
        assert len(checks) == len(cases), checks
        print('UPSTREAM_SSAO_NATIVE_PASSED stereo=%d cases=%d' %
              (value('vulkan_globals.stereo_active'), len(checks)), flush=True)
        call('Cbuf_AddText("quit\\n")')
        gdb.execute('disable 1', to_string=True)
        return
    if next_case == case:
        return
    case = next_case
    quality, half, samples = cases[case]
    for name, setting in [('r_ssao', quality), ('r_ssao_vr_half', half),
                          ('vid_fsaa', samples), ('vid_fsaamode', 0)]:
        call('Cvar_SetValueQuick(&%s,%d)' % (name, setting))
    print('UPSTREAM_SSAO_CASE_BEGIN quality=%d vr_half=%d samples=%d' %
          (quality, half, samples), flush=True)

def end_probe():
    if case < 0 or case in checks or (frame - 8) % 12 < 5:
        return
    quality, half, samples = cases[case]
    stereo = value('vulkan_globals.stereo_active')
    resolution = half if stereo else quality == 1
    assert value('ssao_stereo') == stereo
    assert value('vulkan_globals.sample_count') == samples
    assert value('ssao_mip_pipeline.handle') != 0
    assert value('ssao_evaluate_pipelines[%d].handle' % (quality - 1)) != 0
    assert value('ssao_lookup_set_layout.num_combined_image_samplers') == 2
    assert value('ssao_mip_set_layout.num_combined_image_samplers') == 1
    assert value('ssao_mip_set_layout.num_storage_images') == 6
    for eye in range(2 if stereo else 1):
        for expression in ['mip_descriptors[%d][%d]' % (resolution, eye),
                           'lookup_descriptors[%d][%d]' % (resolution, eye),
                           'prepared_read[%d][%d]' % (resolution, eye)]:
            assert value(expression) != 0, expression
    if stereo:
        assert value('mip_descriptors[%d][0]' % resolution) != value('mip_descriptors[%d][1]' % resolution)
        assert value('lookup_descriptors[%d][0]' % resolution) != value('lookup_descriptors[%d][1]' % resolution)
        assert value('composite_descriptors[%d][3]' % resolution) != 0
    call('Cbuf_AddText("screenshot png\\n")')
    print('UPSTREAM_SSAO_CASE_PASSED stereo=%d quality=%d vr_half=%d samples=%d' %
          (stereo, quality, half, samples), flush=True)
    checks.add(case)
end
break SCR_UpdateScreen
commands
 silent
 python begin_probe()
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
