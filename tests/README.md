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
It also checks inherited VR ladder pitch independence against the ordinary QSS-M
ladder path, and raised-jump-speed non-VR swimming against explicit VR swimming.
Both touch policies are checked through the production helper, including impact
velocities; real collision and nudging exercise fallback to a distinct saved
valid position. The fixture does not run QuakeC touch callbacks or waterjump
launch scaling.
Both slow and optimized hull implementations are selected explicitly; this
fixture does not run cvar registration, so initializer strings alone do not
activate the optimized path.

The generic walking/jumping cases use the QSS-M `03a498aa` single-step path
(`msec == 0`); private explicit-duration cases set `msec` separately. This keeps
the upstream movement reference distinct from the VR wire's substep contract.

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
  -ffunction-sections -fdata-sections tests/pmove_migration_fixture.c \
  Quake/mathlib.c Quake/world.c -Wl,--gc-sections \
  $(pkg-config --cflags --libs sdl3) -lm -o /tmp/quakespasm-pmove-fixture
/tmp/quakespasm-pmove-fixture
```

For the sanitizer check, add `-fsanitize=address,undefined
-fno-sanitize-recover=all -fno-omit-frame-pointer` to that compile/link command.

The collision geometry is constructed by the fixture. Traces and movement are
production code; this is not a dedicated-server, network, tracked-controller or
full reference-parity test. The shared solver is now linked into the game; replay
and server command/QC authority remain separate integration work. Link-time
section collection limits this fixture to movement/collision, while the full
engine build verifies actual module linkage. Console/error stand-ins are
test-only.

## Client movement parameters and production linkage

`pmove_movevars_fixture.c` exercises real full/incremental serverinfo callbacks,
Info readers and PM variable selection. It covers QSS defaults, changed server
values, starred-key behavior, cache invalidation/current protocol flags, public
versus private stat precedence, and nonfinite/out-of-range numeric rejection.
The real command tokenizer covers the former 1,023-byte argument truncation,
rejected oversized tokens, and preservation of settings after missing arguments.
Fractional booleans follow QSS server nonzero semantics. Callback dispatch and
warning output are fixture boundaries; full resource teardown, complete-stat
receipt, network command dispatch and replay are not covered.

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
  -ffunction-sections -fdata-sections \
  -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all \
  -fno-omit-frame-pointer tests/pmove_movevars_fixture.c \
  Quake/pmove.c Quake/cmd.c Quake/common.c Quake/strlcpy.c -Wl,--gc-sections \
  $(pkg-config --cflags --libs sdl3) -lm -o /tmp/qsvr-pmove-movevars-asan
/tmp/qsvr-pmove-movevars-asan
```

`serverinfo_command_smoke.gdb` runs the actual Linux executable before engine
initialization. It registers the real serverinfo callback, injects one wire
message, and calls `CL_ParseServerMessage`. It checks a 2,047-byte command through
to PM gravity selection, and stops at the real `Host_Error` for oversized and
unterminated commands before any callback. It needs GDB and debug symbols, but
no game assets. It does not exercise a socket, normal startup, or error teardown.

```sh
for case_name in fit oversized unterminated; do
  QSVR_SERVERINFO_CASE="$case_name" gdb -nx --batch \
    -x tests/serverinfo_command_smoke.gdb \
    /tmp/quakespasm-2.0-bootstrap-build/vkquake || exit 1
done
```

The consolidated Linux SDL3/debugoptimized build links `pmove.c` through the
ordinary Meson source list. No unresolved server functions are hidden by this
fixture's section collection: disconnected server staging has been removed from
the production module. The existing shared solver and client adapter remain.

## Private snapshot collision bounds

`private_solid_fixture.c` includes the production delta/removal parser and donor
network collision owner, linked against real message readers and the staged PM
weapon query. It
checks public packed bounds, private tags, retained state, following-message
alignment, unknown/truncated encodings and explicit dialect selection. Received
bounds must collide with a synthetic entity box through the actual client trace;
a received non-solid sentinel must remove that collision. Individual and full
replacement removals must clear same-timestamp bounds and invalidate the trace
cache. Negative upper-Z boxes must collide only below their top face with both donor
trace implementations.

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wno-unused-parameter \
  -ffunction-sections -fdata-sections tests/private_solid_fixture.c \
  Quake/common.c Quake/mathlib.c Quake/pmove.c -Wl,--gc-sections \
  $(pkg-config --cflags --libs sdl3) -lm -o /tmp/quakespasm-private-solid
/tmp/quakespasm-private-solid
```

For memory/undefined-behavior checks, add
`-fsanitize=address,undefined -fno-omit-frame-pointer` when compiling and linking.
Ordinary and sanitized runs are supported. Error/console and unused VM, renderer
and transport boundaries are fixture-only stand-ins; no message or collision algorithm is
mocked. Geometry is constructed, and this does not prove transport admission,
live world gathering or prediction. The donor server currently does not transmit
these collision bounds, so its loopback path cannot qualify this behavior.

## Staged private user commands

`private_usercmd_fixture.c` links the actual client/server body codecs and shared
message primitives. Fixed byte arrays check normal and floating-point angles and
extended entity IDs independently of the writer. Roundtrips cover cursor input,
tracked hand/roomscale, akimbo, contacts and both Gorilla formats. Capability
checks preserve the pinned distinction between raw and trusted input, including
the trusted model limit of 4096. Mutated bytes test NaN and both infinities in
time, all float angles and all six cursor components. Exact-size allocations
check every truncated prefix of the complete payloads under sanitizers.

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wno-unused-parameter \
  -ffunction-sections -fdata-sections tests/private_usercmd_fixture.c \
  Quake/cl_input.c Quake/sv_user.c Quake/common.c Quake/mathlib.c \
  -Wl,--gc-sections $(pkg-config --cflags --libs sdl3) -lm \
  -o /tmp/quakespasm-private-usercmd
/tmp/quakespasm-private-usercmd
```

Add `-fsanitize=address,undefined -fno-omit-frame-pointer` for sanitizer coverage.
Keep assertions enabled. Console/error stand-ins and link-time section collection
omit unrelated engine owners; the codecs and wire primitives are not mocked.
These checks do not qualify private admission, clock sampling, redundant command
history, completed-simulation ACKs, replay or networked gameplay.

## Private command history and packet delivery

`private_send_fixture.c` executes the real sender, command writer/reader and
message primitives, capturing only the socket send and disconnect boundaries.
It checks initial command suppression, current-plus-two history, retained
fire/impulse events, monotonic duration and fractional carry, capped long hitches,
16-bit sequence wrapping and a full three-command VR payload within the MTU.
Transport ACKs must remain ordered and queued when the bundle leaves insufficient
space. It also checks public framing despite the colliding extension mask,
demo suppression and send-error disconnect handling.

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wno-unused-parameter \
  -ffunction-sections -fdata-sections tests/private_send_fixture.c \
  Quake/sv_user.c Quake/common.c Quake/mathlib.c -Wl,--gc-sections \
  $(pkg-config --cflags --libs sdl3) -lm -o /tmp/quakespasm-private-send
/tmp/quakespasm-private-send
```

Add `-fsanitize=address,undefined -fno-omit-frame-pointer` for sanitizer checks.
This does not prove packet-loss behavior against a real server, private host-loop
pacing, actual tracking producers or prediction. Those remain integration gates.

## Private movement ACKs and command diagnostics

`private_moveack_fixture.c` includes the production ACK parser and links real
MSG readers. It checks accepted/stale/equal ACKs, 16-bit expansion, the QuakeC
command frame, epoch-triggered smoothing reset calls, Gorilla capability gates,
state sequence/generation, the pinned 4096-model limit, nonfinite state rejection,
every declared truncated prefix of the extended Gorilla payload, and queue
duplicate/overflow handling. Smoothing reset and flush calls are fixture spies;
the fixture does not execute the actual smoothing reset or network flush.
Truncated prefixes retain a larger backing array, so they check logical message
bounds rather than physically truncated allocations.

The command-name regression feeds literal svc 57/ACK bytes followed by opcode
127 through the real MSG/ACK owners, then calls the production diagnostic lookup.
It also covers existing extension IDs, null table entries, negative IDs and
out-of-range IDs. Both shownet and the malformed-command branch use this lookup.
This is a focused lookup/ACK regression, not a full `CL_ParseServerMessage`
dispatcher integration test. The 128-slot table has names only through 56:
the original svc 57 failure was a null `%s` argument, not an out-of-bounds table
read on that path. The lookup guards both cases and preserves known names.

## Staged transport checkpoint: reproducible checks

Run from the `quakespasm-2.0` worktree on branch `2.0`, with assertions enabled.
On 2026-09-22 the sender, move-ACK and solid fixtures all passed ASan/UBSan with
the following commands. Section collection omits unrelated engine owners;
parsing, packet codecs and collision code remain production implementations.

```sh
set -e
for fixture in send moveack solid; do
  case "$fixture" in
    send) sources='Quake/sv_user.c Quake/common.c Quake/mathlib.c' ;;
    moveack) sources='Quake/common.c' ;;
    solid) sources='Quake/common.c Quake/mathlib.c Quake/pmove.c' ;;
  esac
  cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wno-unused-parameter \
    -ffunction-sections -fdata-sections -fsanitize=address,undefined \
    -fno-sanitize-recover=all -fno-omit-frame-pointer \
    "tests/private_${fixture}_fixture.c" $sources -Wl,--gc-sections \
    $(pkg-config --cflags --libs sdl3) -lm -o "/tmp/quakespasm-private-${fixture}-asan"
  "/tmp/quakespasm-private-${fixture}-asan"
done
```

The previous temporary build directory was absent at this checkpoint. Recreate
it if absent, then run the requested consolidated build:

```sh
if [ ! -f /tmp/quakespasm-2.0-bootstrap-build/build.ninja ]; then
  meson setup /tmp/quakespasm-2.0-bootstrap-build . \
    -Duse_sdl3=enabled --buildtype=debugoptimized \
    > /tmp/qsvr-ack-final-setup.log 2>&1
fi
ninja -C /tmp/quakespasm-2.0-bootstrap-build -j12 \
  > /tmp/qsvr-ack-final-build.log 2>&1
```

Result on 2026-09-22: the sender, move-ACK and solid sanitizer fixtures passed.
The consolidated Linux build also passes after the real smoothing reset was
adapted to the donor's `VectorCopy(vec3_origin, ...)` operation. The orchestrator
confirmed Ninja exit 0. The move-ACK fixture spies on smoothing reset calls;
its pass alone does not validate actual smoothing presentation.

These checks do not establish private admission, a matching authoritative owner
snapshot, replay, live Gorilla production, host scheduling parity or dedicated
server movement/fire. See the staged transport findings in
[`migration-movement-review.md`](../docs/migration-movement-review.md).
