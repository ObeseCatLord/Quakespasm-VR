# Standalone VR final qualification, current 2.0 source

Run on 2026-10-01 from `/home/obesecatlord/Documents/quakespasmvr/quakespasm-2.0`, Linux x86_64, GCC 16.2.1, SDL3 3.4.16, Vulkan headers/loader 1.4.357. Initial binaries used `/tmp/qsvr-final-qualification-2.0-DUIuAC`; adapted reruns and direct helper fixtures used `/tmp/qsvr-final-qualification-followup-UQmm1g`. No repository build directory, assets, headset, SSH, benchmark, `make`, production file, or broad input fixture was changed. `tests/README.md` supplied the six adapted recipes; their rerun commands below use the same compiler arguments with the follow-up output prefix.

## Results

Eighteen fixtures passed. The broad controller input fixture remains the single failure: after the shared `BUTTON_ATTACK` fix it links against many absent FBT, weapon-menu/calibration, presentation, and engine stubs; it was left unchanged as directed. All three cases without README recipes (menu anchor and both foveation helpers) passed direct standalone runs. The layout fixture’s SwiftShader invocation separately returned its documented capability skip 77; native Vulkan passed. The six adapted recipe fixtures now pass; initial stale-seam errors and repairs are recorded in their sections.

## Commands and results

### OpenXR session recovery — PASS

```sh
c++ -std=c++14 -DUSE_SDL3 -Wall -Wextra -Werror \
  -Wno-missing-field-initializers tests/openxr_session_recovery_fixture.cpp \
  $(pkg-config --cflags --libs sdl3) -o /tmp/qsvr-final-qualification-2.0-DUIuAC/openxr-session-recovery
/tmp/qsvr-final-qualification-2.0-DUIuAC/openxr-session-recovery
```

Exit 0; `OPENXR_SESSION_RECOVERY_PASSED actual backend creation/event/explicit retry/new frame; simulated runtime/Vulkan dispatch`. It exercises backend ownership and recovery with fake runtime/Vulkan dispatch; it does not establish a live runtime or headset result.

### OpenXR enable — PASS after fixture seam repair

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wno-unused-parameter \
  -ffunction-sections -fdata-sections tests/openxr_enable_fixture.c \
  -Wl,--gc-sections $(pkg-config --cflags --libs sdl3) -lvulkan -lm \
  -o /tmp/qsvr-final-qualification-2.0-DUIuAC/openxr-enable
/tmp/qsvr-final-qualification-2.0-DUIuAC/openxr-enable
```

Initial link failed on undefined `VRXR_VulkanFoveationEyeSupported` and `VRXR_VulkanSwapchainImageFlagsSupported`. Added fixture-local capability spies that report those optional routes unavailable. The exact recipe rerun exited 0 with `OPENXR_ENABLE_PASSED actual command/transition/attach/retirement; simulated runtime, camera/input and empty GPU resources`. README boundary: discovery/queue spies and empty prepared GPU resources prove scheduling/ownership, not loaded-scene or asset continuity.

### OpenXR Vulkan creation — PASS

```sh
c++ -std=c++14 -DUSE_SDL3 -Wall -Wextra -Werror \
  -Wno-missing-field-initializers -pthread \
  tests/openxr_vulkan_creation_fixture.cpp \
  $(pkg-config --cflags --libs sdl3) -lvulkan \
  -o /tmp/qsvr-final-qualification-2.0-DUIuAC/openxr-vulkan-creation
/tmp/qsvr-final-qualification-2.0-DUIuAC/openxr-vulkan-creation
VK_ICD_FILENAMES=/usr/lib/chromium/vk_swiftshader_icd.json \
  /tmp/qsvr-final-qualification-2.0-DUIuAC/openxr-vulkan-creation --real-vulkan
```

Both invocations exited 0. Marker: `OPENXR_VULKAN_CREATION_PASSED merged names/API/features/queues, output matching, threaded callbacks, failure/lifetime`. Real mode also emitted that marker, then `OPENXR_VULKAN_CREATION_REAL_PASSED simulated XR merged extensions, real headless instance/device/multiview`. The latter uses real headless Vulkan creation but simulated XR wrapping; no HMD, frame, scene, or performance claim follows.

### OpenXR late binding — PASS

```sh
c++ -std=c++14 -DUSE_SDL3 -Wall -Wextra -Werror \
  -Wno-missing-field-initializers tests/openxr_late_binding_fixture.cpp \
  $(pkg-config --cflags --libs sdl3) -o /tmp/qsvr-final-qualification-2.0-DUIuAC/openxr-late-binding
/tmp/qsvr-final-qualification-2.0-DUIuAC/openxr-late-binding
```

Exit 0; `OPENXR_LATE_BINDING_PASSED actual discovery/qualification/session/new frame/loss/rediscovery; simulated XR/driver, retained handle identities`. It uses production lifecycle owners with simulated SDL/XR/Vulkan dispatch; no loaded engine scene or GPU work on borrowed XR images.

### OpenXR Vulkan boundary — PASS

```sh
c++ -std=c++14 -DUSE_SDL3 -Wall -Wextra -Werror -Wno-missing-field-initializers \
  tests/vr_openxr_vulkan_fixture.cpp $(pkg-config --cflags --libs sdl3) \
  -o /tmp/qsvr-final-qualification-2.0-DUIuAC/vr-openxr-vulkan
/tmp/qsvr-final-qualification-2.0-DUIuAC/vr-openxr-vulkan
```

Exit 0; `OpenXR Vulkan boundary: creation/version/provenance, image ownership, queue locking, failure unwind and begun-frame retirement passed`. Dispatch is simulated; this does not establish a working device, task-enabled multiview, headset presentation, or gameplay parity.

### OpenXR layout — native PASS; SwiftShader SKIP 77

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -ffunction-sections -fdata-sections \
  tests/openxr_layout_fixture.c Quake/r_ssao.c -Wl,--gc-sections \
  $(pkg-config --cflags --libs sdl3) -lvulkan -lm -o /tmp/qsvr-final-qualification-2.0-DUIuAC/openxr-layout
VK_ICD_FILENAMES=/usr/lib/chromium/vk_swiftshader_icd.json /tmp/qsvr-final-qualification-2.0-DUIuAC/openxr-layout
/tmp/qsvr-final-qualification-2.0-DUIuAC/openxr-layout
```

Compile exited 0. SwiftShader exited 77 with `OPENXR_LAYOUT_SKIPPED ... api=4206592 multiview=1 views=6 sets=4`. Native Vulkan exited 0 and emitted `OPENXR_LAYOUT_PASSED actual donor descriptor/pipeline owners desktop multiview_ready=0 objects=31` and the same marker with `multiview_ready=1`. This calls actual descriptor/pipeline layout owners on a capable Vulkan device; it draws no shaders or scene.

### Render acquisition — PASS after current recorder signature adaptation

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-missing-field-initializers -Wno-sign-compare \
  -ffunction-sections -fdata-sections tests/render_acquire_fixture.c \
  -Wl,--gc-sections $(pkg-config --cflags --libs sdl3) \
  -o /tmp/qsvr-final-qualification-2.0-DUIuAC/render-acquire
/tmp/qsvr-final-qualification-2.0-DUIuAC/render-acquire
```

Initial compile failed because the fixture passed 7 arguments to `R_RecordFrame`, whose current declaration expects 10 (`Quake/r_passes.c:1005`). Adapted it with a null optional query pool, query index 0, and a timestamp-written output assertion; local logging/timestamp symbols stay inside this fixture. The exact recipe rerun exited 0: `Render acquisition boundary: desktop/stereo OIT/SSAO/MSAA variants, view masks, output layouts and unowned presentation suppression passed`. Production recorder uses Vulkan command spies; no actual GPU execution or task scheduling is qualified.

### Stereo camera — PASS after fixture seam repair

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wno-unused-parameter \
  -ffunction-sections -fdata-sections tests/vr_stereo_camera_fixture.c \
  Quake/mathlib.c Quake/vr_locomotion.c Quake/cl_input.c -Wl,--gc-sections $(pkg-config --cflags --libs sdl3) -lm \
  -o /tmp/qsvr-final-qualification-2.0-DUIuAC/stereo-camera
/tmp/qsvr-final-qualification-2.0-DUIuAC/stereo-camera
```

The first attempt failed on `VectorClear`; the main owner replaced it with `VectorCopy(vec3_origin, prediction_view_offset)`. The next link exposed the listed renderer/client symbols. Added narrow fixture seams for a prepared dry leaf, closed menu, disabled alpha sorting, and absent prediction correction. The exact recipe rerun exited 0 and reported passes for eye separation/pause restore/abort drain; floor, scale and comfort; head aiming and transitions; local turning; hand body offsets; and roomscale camera anchor. Wait and chase collision remain spies, so this does not reproduce GPU buffer reuse or the full OpenXR begin/task path.

```text
Production stereo camera: eye separation, pause restoration, skipped reference invalidation and abort GPU-drain boundary passed
Inherited floor/scale/comfort: floor height, crouch, pitched basis, paused viewheight, LOCAL fallback and desktop gates passed
Head aiming: actual command angles and paused visual view contain one head rotation
Aim transitions: reference loss, mode changes, authoritative angles, centerview, pending cancellation/priority, locked accumulation, real chase and client clear passed
Local turning: effective command basis, all-mode rebase retention, single commit and authority precedence passed
Hand body offsets: shared eye height, LOCAL/reference rebases, axis yaw, invalid poses and pre-camera reference passed
Roomscale camera anchor: pending/sent private body motion removes duplicate horizontal HMD offset; public and vertical paths remain distinct
```

### Broad controller input — FAIL (link after shared-source repair)

```sh
cc -std=gnu11 -DUSE_SDL3 -Wall -Wextra -Werror \
  -Wno-missing-field-initializers -Wno-unused-parameter \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -ffunction-sections -fdata-sections tests/vr_input_fixture.c \
  Quake/vr_input.c Quake/vr_locomotion.c Quake/mathlib.c -Wl,--gc-sections \
  $(pkg-config --cflags --libs sdl3) -lm \
  -o /tmp/qsvr-final-qualification-2.0-DUIuAC/controller-input
/tmp/qsvr-final-qualification-2.0-DUIuAC/controller-input
```

Initial compile failed on missing `BUTTON_ATTACK` in `Quake/vr_input.c`. Main owner integrated the existing `sharedpmove.h` bit; rerun compiled and then failed to link the fixture’s missing stubs. Undefined references include `VR_WeaponMenu_*`, `VR_WeaponCalibration*`, `VR_FBT_*`, presentation/akimbo helpers, engine command/console symbols and key bindings. No executable/PASS marker. README itself says updated FBT, weapon-menu and calibration stubs are needed. Its recording key sink does not qualify native binding execution or a physical controller.

### Input continuity — PASS after shared-source repair

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wno-unused-parameter \
  -ffunction-sections -fdata-sections -fsanitize=address,undefined \
  -fno-omit-frame-pointer tests/vr_input_continuity_fixture.c Quake/mathlib.c \
  -Wl,--gc-sections $(pkg-config --cflags --libs sdl3) -lm \
  -o /tmp/qsvr-final-qualification-2.0-DUIuAC/input-continuity
ASAN_OPTIONS=detect_leaks=0 /tmp/qsvr-final-qualification-2.0-DUIuAC/input-continuity
```

Initial compile failed on the same missing `BUTTON_ATTACK`; rerun after the shared header change exited 0: `VR_MOTION_CONTINUITY_PASSED fresh roomscale baseline; pending contact/Gorilla/turn discard; analog rearm/snap latches retained; full invalidation unchanged`. It executes tracking reset, roomscale accumulation, release and neutral helpers with prepared gameplay context and a recording `Key_Event` sink. It does not execute the complete held-input/contact submission pipeline or a headset runtime. Leak detection was disabled as documented.

### Input keys — PASS

```sh
for source in keys common; do
  cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE \
    -ffunction-sections -fdata-sections -Wno-unused-parameter \
    -c "Quake/$source.c" $(pkg-config --cflags sdl3) \
    -o "/tmp/qsvr-final-qualification-2.0-DUIuAC/controller-$source.o" || exit 1
done
cc -std=gnu11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
  tests/vr_input_keys_fixture.c /tmp/qsvr-final-qualification-2.0-DUIuAC/controller-keys.o \
  /tmp/qsvr-final-qualification-2.0-DUIuAC/controller-common.o -Wl,--gc-sections -o /tmp/qsvr-final-qualification-2.0-DUIuAC/controller-keys
/tmp/qsvr-final-qualification-2.0-DUIuAC/controller-keys
```

Exit 0; `VR native keycodes preserve existing gamepad ranges and MAX_KEYS capacity`. It checks production key-name converters/table capacity, not physical input.

### Aim — PASS

```sh
cc -std=c11 -Wall -Wextra -Werror tests/vr_aim_fixture.c -lm \
  -o /tmp/qsvr-final-qualification-2.0-DUIuAC/aim
/tmp/qsvr-final-qualification-2.0-DUIuAC/aim
```

Exit 0; `Inherited VR aim arithmetic preserves pose signs and modes`. This is pose arithmetic coverage only, not controller/runtime integration.

### Menu anchor — PASS; no README recipe

`cc -std=c11 -Wall -Wextra -Werror tests/vr_menu_anchor_fixture.c -lm -o /tmp/qsvr-final-qualification-followup-UQmm1g/menu-anchor` then `/tmp/qsvr-final-qualification-followup-UQmm1g/menu-anchor` exited 0: `VR menu anchor fixture passed`. This exercises the header-only presentation anchor helper; it does not establish an in-renderer panel or headset result.

### Foveation policy — PASS; no README recipe

`cc -std=c11 -Wall -Wextra -Werror -IQuake tests/vr_foveation_policy_fixture.c -lm -o /tmp/qsvr-final-qualification-followup-UQmm1g/foveation-policy` then `/tmp/qsvr-final-qualification-followup-UQmm1g/foveation-policy` exited 0 with no output. Header-only mode/stability/gaze policy assertions; no runtime or GPU foveation is qualified.

### Foveation rate map — PASS; no README recipe

`cc -std=c11 -Wall -Wextra -Werror -IQuake tests/vr_foveation_rate_map_fixture.c -lm -o /tmp/qsvr-final-qualification-followup-UQmm1g/foveation-rate-map` then `/tmp/qsvr-final-qualification-followup-UQmm1g/foveation-rate-map` exited 0 with no output. This checks CPU rate-map extent, encoding, edge tiles, layers, and invalid-input handling; it does not submit a rate map to a runtime or GPU.

### Weapon schema — PASS

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
  -ffunction-sections -fdata-sections \
  -IQuake tests/vr_weapon_schema_fixture.c Quake/vr_weapon_schema.c \
  Quake/common.c -Wl,--gc-sections $(pkg-config --cflags --libs sdl3) -lm \
  -o /tmp/qsvr-final-qualification-2.0-DUIuAC/weapon-schema
/tmp/qsvr-final-qualification-2.0-DUIuAC/weapon-schema
```

Exit 0; `weapon schema fixture passed`. It tests the bounded staging parser with the native tokenizer; this is parser/schema coverage, not rendered weapon alignment.

### Weapon calibration — PASS after fixture seam repair

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
  -ffunction-sections -fdata-sections -fsanitize=address,undefined \
  -fno-sanitize-recover=all -fno-omit-frame-pointer -IQuake \
  tests/vr_weapon_calibration_fixture.c Quake/vr_weapon_calibration.c \
  Quake/vr_locomotion.c Quake/mathlib.c \
  -Wl,--gc-sections $(pkg-config --cflags --libs sdl3) -lm \
  -o /tmp/qsvr-final-qualification-2.0-DUIuAC/weapon-calibration
/tmp/qsvr-final-qualification-2.0-DUIuAC/weapon-calibration
```

Initial link lacked `Cvar_SetCallback` and `Con_Warning`; after adding their narrow fixture seams, execution exposed the cvar table’s new preset entry. Capacity now includes that one registered preset cvar in addition to the existing 99 weapon slots. The exact recipe rerun exited 0: `VR weapon calibration fixture passed`. Existing calibration/schema/projectile-source assertions remain active, including shared solo/co-op values and legacy per-weapon MP-field behavior.

### Calibration save — PASS after current helper adaptation

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
  -ffunction-sections -fdata-sections -IQuake \
  tests/vr_weapon_calibration_save_fixture.c Quake/vr_weapon_schema.c \
  Quake/common.c -Wl,--gc-sections $(pkg-config --cflags --libs sdl3) -lm \
  -o /tmp/qsvr-final-qualification-2.0-DUIuAC/weapon-calibration-save
/tmp/qsvr-final-qualification-2.0-DUIuAC/weapon-calibration-save
```

Initial compile used obsolete 5-argument calls. The fixture now passes `global_mode=false` for this per-entry single-calibration rewrite and derives original count/metadata with the production parser for the saved-values validator. The exact recipe rerun exited 0: `weapon calibration save fixture passed`. It still asserts removal of per-weapon legacy MP keys, comments, absent classic muzzles, and new-entry placement. The waived top-level `global_mp_held_offset` setup was replaced with the current `global_held_offset` setting.

### Calibration reload — PASS after fixture seam repair

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
  -ffunction-sections -fdata-sections -fsanitize=address,undefined \
  -fno-sanitize-recover=all -fno-omit-frame-pointer -IQuake \
  tests/vr_weapon_calibration_reload_fixture.c \
  Quake/vr_weapon_calibration.c Quake/vr_weapon_schema.c \
  Quake/vr_locomotion.c Quake/mathlib.c Quake/common.c \
  -Wl,--gc-sections -Wl,--wrap=COM_LoadFile \
  $(pkg-config --cflags --libs sdl3) -lm \
  -o /tmp/qsvr-final-qualification-2.0-DUIuAC/weapon-calibration-reload
/tmp/qsvr-final-qualification-2.0-DUIuAC/weapon-calibration-reload
```

Initial link lacked `Cvar_SetCallback` and `Con_Warning`; after adding fixture-local implementations, the cvar count also needed room for the registered preset cvar while retaining the 99 weapon slots. Removed only the waived legacy `global_mp_muzzle_offset` overlay input; Enyo identity/default and explicit per-weapon override assertions remain. The exact recipe rerun exited 0: `VR weapon calibration reload fixture passed`. `COM_LoadFile` remains wrapped; this does not qualify native active search-path loading. README’s old overlay description remains unchanged because README is outside this write set.

## Qualification limits

Passing fake-dispatch/helper fixtures establish their recorded state-transition, API-shape, parser, or command-boundary assertions. None establishes complete rendered gameplay or headset output. In particular, the Vulkan layout fixture creates layouts but does not draw; creation and late-binding simulate XR; camera/input helpers use spies, prepared contexts, or recording sinks. Live headset, gaze, performance, and platform acceptance remain outside this slice.
