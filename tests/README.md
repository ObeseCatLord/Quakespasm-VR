# Migration boundary fixtures

`vr_openxr_vulkan_fixture.cpp` is reused from the product OpenXR branch at
`3080841333fa94000df7e1fb9e549c7158685dd6`, adapted to the Vulkan-only 2.0 backend.
It checks creation-result ownership, instance/device provenance, version
requirements, independent/array eye ownership, incomplete-frame suppression,
balanced queue callbacks on runtime failures, and unlocked renderer retirement
before runtime image destruction. Begun-frame healthy detach checks that the
queue callbacks remain usable for release/end after retirement and survive for
reattachment; full shutdown clears them. The dispatch is simulated;
passing does not prove a working Vulkan device, task-enabled multiview, headset
presentation or gameplay parity.

After the implementation slice is ready for consolidated checks, an SDL3 build is:

```sh
c++ -std=c++14 -DUSE_SDL3 -Wall -Wextra -Werror -Wno-missing-field-initializers \
  tests/vr_openxr_vulkan_fixture.cpp $(pkg-config --cflags --libs sdl3) \
  -o /tmp/quakespasm-2.0-openxr-vulkan-fixture
/tmp/quakespasm-2.0-openxr-vulkan-fixture
```

For SDL2, omit `-DUSE_SDL3` and use `pkg-config --cflags --libs sdl2`.
The fixture must retain assertions (do not define `NDEBUG`). No runtime or headset
is required; it does not open a session against an installed runtime.

## Presentation image acquisition

`render_acquire_fixture.c` executes the production frame description and recorder
with Vulkan command spies. Across ordinary transparency, WBOIT, MBOIT, and SSAO
on/off, it checks that a failed acquisition leaves scene/prepared commands intact
and executes neither the UI/presentation framebuffer nor screenshot readback.
The failed-acquisition cases deliberately have no UI framebuffer and an invalid
image index. Successful acquisition still executes both UI subpasses and readback.
This does not validate actual GPU execution or task scheduling.

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-missing-field-initializers -Wno-sign-compare \
  -ffunction-sections -fdata-sections tests/render_acquire_fixture.c \
  -Wl,--gc-sections $(pkg-config --cflags --libs sdl3) \
  -o /tmp/quakespasm-2.0-render-acquire-fixture
/tmp/quakespasm-2.0-render-acquire-fixture
```

Keep assertions enabled. Section garbage collection omits unrelated resource
creation code; the fixture includes the actual recorder rather than a duplicate
implementation. Windows and ARM verification are deferred until the end; these
commands cover this Linux development machine only.

## Stereo projection, camera and skipped-frame recovery

The acquisition fixture also creates the production scene/UI pass descriptions
with Vulkan creation spies for desktop and stereo, MSAA off/4x and all three
transparency modes. It checks view masks and final output layouts; SSAO remains
an effective desktop-only option during this integration checkpoint. These are
API-shape checks, not driver validation.

`vr_openxr_math_fixture.c` compares the production relative clip correction with
an independently calculated direct eye projection across asymmetric/canted eyes,
head rotation, translation, IPD, reversed depth and invalid inputs.

```sh
cc -std=c11 -Wall -Wextra -Werror tests/vr_openxr_math_fixture.c -lm \
  -o /tmp/quakespasm-stereo-math
/tmp/quakespasm-stereo-math
```

`vr_stereo_camera_fixture.c` calls the actual renderer camera preparation,
restoration, reference invalidation and host-abort cleanup functions. It checks
Quake-axis eye separation, repeated paused-camera restoration, reference
invalidation across a skipped frame, and the GPU-drain call before XR abort.
Its wait is a spy: it does not reproduce real pending GPU buffer reuse or
exercise the entire OpenXR begin/task path. Those remain runtime acceptance cases.

It also runs production `V_CalcRefdef` and comfort functions, checking floor-known
height, crouching, scale/IPD changes, a pitched camera basis, a paused base whose
server viewheight subsequently changes, `LOCAL` relative-height fallback,
invalid scale values, and desktop/VR bob, movement/death roll and damage/gun-kick
gates. It also checks recoil recovery while suppressed and classification of a
completed chase-camera base. The real donor chase calculation runs with a stubbed
hull trace that provides a known collision result; the live smoke uses real map
collision.

The same fixture links production `CL_BaseMove` and checks head aim in actual
command angles, one visual head contribution, reference rebasing across lost
tracking, mode changes, server absolute/relative angles, centerview, pending
server-yaw cancellation/priority, and client resets. Modes 1/2 accumulate visual
head movement through angle locks without changing locked commands, then publish
it once on unlock, including centerview between lock expiry and frame update.
A paused new client prepares a fresh base once. Modes 3/4/7 drive the real chase calculation and retain fresh
orientation while paused. These checks do not establish controller, roomscale,
full camera or QSS-M networking parity.

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wno-unused-parameter \
  -ffunction-sections -fdata-sections tests/vr_stereo_camera_fixture.c \
  Quake/mathlib.c Quake/cl_input.c -Wl,--gc-sections $(pkg-config --cflags --libs sdl3) -lm \
  -o /tmp/quakespasm-stereo-camera
/tmp/quakespasm-stereo-camera
```

`vr_aim_fixture.c` checks the reused pose signs, modes 1–7, blended deadzone,
null-controller behavior and rejection of nonfinite pose input independently:

```sh
cc -std=c11 -Wall -Wextra -Werror tests/vr_aim_fixture.c -lm \
  -o /tmp/quakespasm-aim-math
/tmp/quakespasm-aim-math
```

The image-ownership fixture additionally checks metadata queries before image
acquisition, bounds/terminal rejection, and invalidation on shutdown. Metadata
allows renderer view creation; it never authorizes access to image contents.

## Window-size ownership

`window_size_fixture.c` invokes the production SDL resize helper. It reproduces
a delayed desktop-size event after XR attachment, checks a further window resize
without changing the runtime target, and checks ordinary desktop behavior.

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wno-unused-parameter \
  -ffunction-sections -fdata-sections tests/window_size_fixture.c \
  -Wl,--gc-sections $(pkg-config --cflags --libs sdl3) -lm \
  -o /tmp/quakespasm-window-size-fixture
/tmp/quakespasm-window-size-fixture
```

## Local OpenXR GPU smoke

This optional integration check needs a debug-symbol Linux build, GDB with
Python support, Vulkan synchronization validation, Quake data, and a working
Monado build with its simulated driver. It is not a performance benchmark.
Use a disposable game directory with `id1/pak0.pak` and `id1/pak1.pak` copied or
symlinked from your licensed installation; the test writes config there.

Start an isolated Monado service in a separate terminal with a private
`XDG_RUNTIME_DIR` and `XDG_CONFIG_HOME`, `SIMULATED_ENABLE=1` and
`XRT_COMPOSITOR_FORCE_XCB=1`. Give it a terminal for stdin. Use that build's
matching OpenXR runtime manifest. See the official
[Monado development instructions](https://monado.freedesktop.org/developing-with-monado.html).
Do not change the user's default runtime or stop another running service.

In the test terminal, set `XR_TEST_ROOT` to the disposable test directory,
`XR_TEST_BINARY` to the debug-symbol executable, and `XR_RUNTIME_JSON` to that
matching manifest. Export the same private `XDG_RUNTIME_DIR` and
`XDG_CONFIG_HOME` used by the service, and a private `XDG_DATA_HOME`.
If the validation layer is outside system search paths, set `VK_LAYER_PATH` to
its manifest directory. Then run from the repository root:

```sh
cp tests/openxr-local-smoke.cfg "$XR_TEST_ROOT/game/id1/"
SDL_VIDEODRIVER=x11 timeout --signal=TERM 110s \
  gdb --return-child-result -batch -x tests/openxr-local-smoke.gdb --args \
  "$XR_TEST_BINARY" -validation 2 -basedir "$XR_TEST_ROOT/game" \
  -window -width 640 -height 480 -nosound -openxr +map start \
  > "$XR_TEST_ROOT/gpu-smoke.log" 2>&1
```

Require exit 0, all 24 `XR_SMOKE_probe` records, the final extent check, and no
Vulkan validation errors or synchronization hazards. A timeout is a failure.
The script asserts effective OIT, sample count and indirect state for each
combination rather than accepting unsupported modes as coverage. It also checks
that task rendering is effective. The final probes change worldscale and floor
offset live, including while paused, and recover from an invalid zero scale.
They also enable the real chase camera at scale 2, then return to first person.
Head-aim and mouse-aim probes compare the rendered visual yaw with command and
local-server angles; holding attack must consume shells. Final chase probes
check resolved orientation with running and paused simulation.
They require a floor-referenced simulated runtime, and compare world height to
the inherited formula and eye separation to the runtime pose. `LOCAL` fallback
is covered by the production camera fixture, not this GPU run.
A GPU that cannot provide the requested modes
fails this qualification; that does not by itself mean desktop/XR is unsupported.

For visual evidence, optionally export `XR_SMOKE_CAPTURE` to an output `.png`
path before running. With ImageMagick `import` installed and only this test's
XCB compositor window named `Monado`, the harness saves `-initial.png`,
`-resized.png`, `-scaled.png`, `-raised.png`, `-chase.png`, `-head-aim.png`,
`-mouse-aim.png` and `-chase-paused.png` variants of that path.
Inspect both eyes for full scene coverage,
stereo differences and artifacts. Capture failures fail the check.

The GDB gate inserts the script into the existing command queue after client
signon. Do not replace this with startup `+exec`: map-loading keepalive refreshes
can consume waits before the world is live. Each `viewpos` probe follows 75
waits, and reads the settled renderer state without changing its ownership.
The test does not validate moving/scaled avatars, eye-only visibility, inherited
tracked weapons/controller input, headset behavior or performance. Stop only the isolated service
you started when done. Keep raw logs local; they can contain device identifiers.

## Staged shared movement solver

`pmove_migration_fixture.c` runs the transplanted PMove algorithm against the
real vkQuake hull functions in `world.c`. It covers walking/floor contact, jumping,
frozen commands, once-per-command roomscale across substeps, outlier rejection,
entity boxes, rotated brush normals, stationary/startsolid, water contents and source-equivalent raw clip leaves.
Both slow and optimized hull implementations are selected explicitly; this
fixture does not run cvar registration, so initializer strings alone do not
activate the optimized path.

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
  -ffunction-sections -fdata-sections tests/pmove_migration_fixture.c \
  Quake/mathlib.c Quake/world.c -Wl,--gc-sections \
  $(pkg-config --cflags --libs sdl3) -lm -o /tmp/quakespasm-pmove-fixture
/tmp/quakespasm-pmove-fixture
```

The collision geometry is constructed by the fixture. Traces and movement are
production code; this is not a dedicated-server, network, tracked-controller or
full reference-parity test. The solver is intentionally not linked into the game
until real command/replay and server/QC owners are integrated. Link-time section
collection excludes those unimplemented owner calls from this focused fixture;
no dummy gameplay implementations satisfy them. Its console/error functions and
unused cvar/session boundary stand-ins are test-only.
