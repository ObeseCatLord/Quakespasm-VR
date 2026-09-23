# Migration boundary fixtures

## VR weapon schema parser

`vr_weapon_schema_fixture.c` exercises the bounded staging parser against the
native `COM_ParseExBuffer` tokenizer from `common.c`. It covers installed id1
and qbj3/ad-style entries, sequential global inheritance, donor aliases,
ownership/stat mapping, enhanced offsets, malformed input and the 64-entry
limit. Run it on Linux with:

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
  -ffunction-sections -fdata-sections \
  -IQuake tests/vr_weapon_schema_fixture.c Quake/vr_weapon_schema.c \
  Quake/common.c -Wl,--gc-sections $(pkg-config --cflags --libs sdl3) -lm \
  -o /tmp/quakespasm-vr-weapon-schema-fixture
/tmp/quakespasm-vr-weapon-schema-fixture
```

`vr_weapon_calibration_fixture.c` checks the live calibration authority's
classic cvar names and lifetimes, field-wise schema merging, live classic
muzzle edits, classic/enhanced multiplayer overlays, invalid and missing
profiles, reset behavior, and all 99 cvar slots. A full-slot insertion failure
must leave the existing calibration and all free slots unchanged. It also calls
the production projectile source helper to check the pitched 8-unit forward
and 16-unit world-up default, grenade origin, schema right/up/forward offsets,
optional view height, self-origin overrides, and independence from MP muzzle
overlays.

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
  -ffunction-sections -fdata-sections -fsanitize=address,undefined \
  -fno-sanitize-recover=all -fno-omit-frame-pointer -IQuake \
  tests/vr_weapon_calibration_fixture.c Quake/vr_weapon_calibration.c \
  Quake/vr_locomotion.c Quake/mathlib.c \
  -Wl,--gc-sections $(pkg-config --cflags --libs sdl3) -lm \
  -o /tmp/quakespasm-vr-weapon-calibration-fixture
/tmp/quakespasm-vr-weapon-calibration-fixture
```

`vr_weapon_model_selector_fixture.c` calls the production command-time muzzle
selector with a stub for native `Mod_Extradata_CheckSkin`. It covers weapon
switches before `viewent.model` refresh, invalid indices/models/headers, all
four supported pose formats, skin forwarding, classic and enhanced multiplayer
overlays, missing profiles, and nonfinite calibration values.

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
  -ffunction-sections -fdata-sections -IQuake \
  tests/vr_weapon_model_selector_fixture.c Quake/vr_weapon_calibration.c \
  -Wl,--gc-sections $(pkg-config --cflags --libs sdl3) -lm \
  -o /tmp/quakespasm-vr-weapon-model-selector-fixture
/tmp/quakespasm-vr-weapon-model-selector-fixture
```

`vr_weapon_calibration_reload_fixture.c` exercises active-game reload with the
real schema parser, including all eight enhanced muzzle fallbacks, missing-file
behavior, field-wise file overrides, profile replacement on a later reload,
all ten Enyo classic held/muzzle defaults, Enyo identity-only schema plus
global MP overlay, file freeing, and safe built-in retention after malformed input. The file
loader is a fixture boundary; native `COM_LoadFile` supplies active search-path
behavior in the game.

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
  -ffunction-sections -fdata-sections -fsanitize=address,undefined \
  -fno-sanitize-recover=all -fno-omit-frame-pointer -IQuake \
  tests/vr_weapon_calibration_reload_fixture.c \
  Quake/vr_weapon_calibration.c Quake/vr_weapon_schema.c Quake/common.c \
  -Wl,--gc-sections -Wl,--wrap=COM_LoadFile \
  $(pkg-config --cflags --libs sdl3) -lm \
  -o /tmp/quakespasm-vr-weapon-calibration-reload-fixture
/tmp/quakespasm-vr-weapon-calibration-reload-fixture
```

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

The camera fixture also checks the body-relative roomscale anchor: prepared
private VR commands establish view ownership without duplicate horizontal HMD
translation while height and IPD remain live. It covers blocked/accepted body
steps, reference changes, focus loss, mode changes, public/local guards and
chase views. The VR command producer is still dormant on the wire, so these
checks do not establish live roomscale gameplay.

It also checks the shared renderer head-height reference used by raw
body-relative hand grips: floor and LOCAL height formulas, yaw/axis signs,
reference rebases, invalid poses, and a hand query before camera preparation.

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wno-unused-parameter \
  -ffunction-sections -fdata-sections tests/vr_stereo_camera_fixture.c \
  Quake/mathlib.c Quake/vr_locomotion.c Quake/cl_input.c -Wl,--gc-sections $(pkg-config --cflags --libs sdl3) -lm \
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
message, and calls `CL_ParseServerMessage`. It checks the old 2,047-byte command
boundary, a 4,096-byte info string and the full 8,191-byte info capacity through
to PM gravity selection. Oversized/unterminated wire strings and oversized
ordinary commands stop at the real `Host_Error` before callbacks. Trailing
comments remain valid. Separate direct server-command dispatch cases reject
quoted/unquoted token overflow before a valid prefix can execute; public
tokenizer side effects are also checked. It needs GDB and debug symbols, but
no game assets. It does not exercise a socket, normal startup, or error teardown.

```sh
for case_name in fit beyond2k capacity oversized unterminated \
  comment_line comment_block token_overflow token_overflow_quoted ordinary_oversized; do
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
demo suppression and send-error disconnect handling. Public 8-bit/16-bit, public
predinfo and private packet cases deliberately use prepared command angles that
differ from global view angles; they verify encoding precision, journal agreement,
packet widths and complete payload consumption.

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


## Pinned dedicated-peer preparation

The movement reference is product commit
`1327f795cc2e3a8e4f7c9d68e31d64383930cc00`. A native Linux debug build from an
untouched `git archive` of that commit succeeds with `make -C Quake -f
Makefile.linux -j12 DEBUG=1`. Build the archive in a temporary directory so no
source branch or installed executable changes. This host build is diagnostic,
not a GLIBC-qualified release artifact.

Use the canonical `quakespasm_straight` installation specified by the product
`AGENTS.md` for game assets. Create separate temporary reference/client `id1`
directories and link only its numbered `pak*.pak` archives. Do not assume an
`id1/pak1.pak` exists: the current canonical installation has a combined
`pak0.pak` with 1,121 entries, including `progs.dat` and `maps/e1m1.bsp`.
Keep generated configurations, saves and logs in the temporary profiles.
The same installation also has loose `mjolnir/maps/mj4m1.bsp` and `.lit` assets
for the later large-map benchmark; their presence is not a performance result.

The reference binary and profiles have been prepared locally. They have **not**
yet established a successful migrated-client connection or predicted gameplay.
Connection admission, coherent owner/ACK selection and replay must be integrated
before using them for the dedicated-peer movement/fire acceptance proof.


## Private owner association: complete-message boundary

`private_owner_snapshot_fixture.c` uses production ACK/update readers and the
message-end commit helper. It covers accepted/stale/equal ACKs, epoch changes,
standalone invalidation, repeated owner resets, removal/world reset, owner
changes, nonfinite state and logical truncation prefixes. Its packet-loss case
seeds stale prior state and applies a repeated reset; it does not simulate a
socket or loss scheduling. Rendering/network/QC boundaries are test-only stand-ins.

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wno-unused-parameter \
  -ffunction-sections -fdata-sections -fsanitize=address,undefined \
  -fno-sanitize-recover=all -fno-omit-frame-pointer \
  tests/private_owner_snapshot_fixture.c Quake/common.c -Wl,--gc-sections \
  $(pkg-config --cflags --libs sdl3) -lm -o /tmp/qsvr-private-owner-asan
/tmp/qsvr-private-owner-asan
```

`private_owner_snapshot_smoke.gdb` calls the actual `CL_ParseServerMessage`
inside the Linux debug executable with injected wire bytes before engine
initialization. It verifies matching owner/ACK publication only at message end,
a later accepted standalone ACK, an ignored stale ACK, actual `svc_setview`
changes away and back, later owner omission, and a truncated trailing ACK.
The malformed case stops at the real `Host_Error` before any candidate commit.

```sh
gdb -nx --batch -x tests/private_owner_snapshot_smoke.gdb \
  /tmp/quakespasm-2.0-bootstrap-build/vkquake
```

The check seeds client/entity state and bypasses startup and sockets. It proves
whole-message dispatch behavior, not connection admission, prediction/replay,
gameplay, or error teardown. A matched owner baseline does not itself grant
prediction permission; that remains the consumer's policy decision.

## Pinned-peer admission and desktop gameplay

`private_time_version_smoke.gdb` checks actual complete-message parsing without
assets. Private `svc_time` has no sequence short; public PREDINFO retains it.
Adjacent stat updates prove alignment. Private non-RMQ `svc_version` is rejected
before changing the base protocol, while ordinary public versions remain valid.

```sh
for case_name in public_predinfo_time public_plain_time private_time \
  private_nonrmq_version private_rmq_version public_netquake_version public_fitz_version; do
  QSVR_TIME_VERSION_CASE="$case_name" gdb -nx --batch \
    -x tests/private_time_version_smoke.gdb /tmp/quakespasm-2.0-bootstrap-build/vkquake || exit 1
done
```

`private_header_admission_smoke.gdb` requires normal engine initialization and
an isolated asset profile. Its cases are `valid`, `missing_bit`, `extra_bit`,
`nonzero_pext1`, `wrong_flags`, `more_flags`, `truncated`, `no_extensions`, and
`public_private_header`, selected with `QSVR_HEADER_CASE`. The valid tuple ends
with an intentionally invalid maxclients sentinel: it checks admission without
loading a world. All malformed tuples must fail before private admission.
Pass the profile through ordinary `--args vkquake -basedir <profile> -novr ...`.

The live probes use a separately running unchanged `1327f795` dedicated peer,
the canonical Straight `id1/pak0.pak` linked into temporary profiles for reading,
and the migrated diagnostic Linux binary. Do not run them in the deployed game
folder: lifecycle testing records a demo and local startup writes configuration.
The pinned peer accepts `-dedicated 4 -ip 127.0.0.1 -port 28771 +coop 1 +map e1m1`;
use a real terminal with `TERM=xterm` when issuing `changelevel` interactively.
Both processes need their own isolated `-basedir`.

`pinned_peer_gameplay_smoke.gdb` takes `QSVR_PEER_RESULT=<output.json>` and normal
client launch arguments, including `+connect 127.0.0.1:28771 qsvr1`. It injects
held key state, exercises real input/transport/simulation, and checks signon,
authoritative movement, shell consumption, prediction and settling. Setting
`QSVR_PEER_CHANGELEVEL=<ready.json>` writes a readiness file after settling;
issue `changelevel e1m2` to the dedicated peer, and the probe also requires
completed signon and prediction permission in the new world.

`pinned_vr_gameplay_smoke.gdb` is the focused OpenXR controller extension of
that peer proof. Start an isolated simulated Monado service and the unchanged
pinned dedicated server as above, with separate disposable `-basedir` profiles.
The migrated profile must also expose the active game's `vr_weapons.txt` (the
canonical Straight `id1` file is read-only). Supply the private runtime's
matching `XR_RUNTIME_JSON`, private XDG directories, and
`QSVR_PINNED_VR_RESULT=<output.json>`, then run the migrated debug binary with
`-openxr -nosound -window -width 640 -height 480 -basedir <migrated-profile>
+connect 127.0.0.1:<port> qsvr1` under `timeout --signal=TERM 130s gdb -nx
--batch -x tests/pinned_vr_gameplay_smoke.gdb --args ...`. The probe injects
only controller pose/actions into the completed OpenXR frame; the runtime HMD,
native bindings, command builder, transport and dedicated server remain live.
It requires focused stereo/private signon, the active shotgun's ten-unit muzzle
calibration, a finite relative VR attack command, a covering server ACK and
authoritative shell consumption. It writes a compact JSON result and prints
`QSVR_PINNED_VR_PASSED`. This does not establish muzzle world origin, damage,
roomscale collision or visible weapon alignment.

For the donor server's **pre-clamp** muzzle relation, run the server under
`tests/pinned_vr_server_muzzle_smoke.gdb` with
`QSVR_PINNED_SERVER_RESULT=<output.json>` and its own disposable basedir/port.
The script observes remote client 0 at `SV_ClampVRMuzzleToWorld` and writes an
atomic, bounded JSON result after each matching attack sample. Run the client
probe above against that port, then stop the owned GDB server. Require client
success and server `status: passed`, at least three valid samples and no
observer error. The server checks stored pose against accepted command,
weapon-use pitch/yaw against stored hand aim, and pre-clamp muzzle against
authoritative origin plus relative hand pose. It does not inspect the
post-clamp muzzle or QuakeC projectile/damage effects.

`pinned_peer_lifecycle_smoke.gdb` uses the same client launch and accepts
`QSVR_LIFECYCLE_RESULT=<output.json>`, with optional `QSVR_PEER_ADDRESS` overriding
the local peer address. It checks public rejection of the private header,
real error teardown, a failed private connection to a bound silent local UDP
socket, private reconnection, local-map startup and public demo playback.
It handles the diagnostic build's intentional `Host_Error` debug trap before
checking native teardown. These are desktop loopback checks, not physical
tracking, private-demo parity, packet-loss tolerance or performance benchmarks.

For partial-command presentation, set `QSVR_PEER_EXPECT_PARTIAL=1` and use
`+host_maxfps 144 +host_phys_max_ticrate 20`. The gameplay probe then requires
multiple rendered position changes while both the sent command number and
authoritative owner state remain unchanged. This checks between-send prediction,
not frame-rate or latency performance.

## Non-consuming input and replay presentation

`client_input_preview_fixture.c` verifies that zero, one and repeated previews
preserve key/button edges, impulses, pending device input, history, sequence and
sampler state, and produce identical subsequent final command bytes. It uses
the production input builders.

`client_replay_fixture.c` exercises the production replay/presentation helpers
with PM probes: partial timing, empty history, prediction opt-out, protocol and
owner/ACK gates, history loss, epochs, Gorilla provenance, and attachment parent
order. `client_replay_solver_fixture.c` instead uses the actual shared PM solver
and donor collision functions to check empty-history, zero-duration underwater
categorization. Its input preview and world-entity collection are fixture seams;
it does not replace the separate live gameplay check.

```sh
set -e
for fixture in client_input_preview client_replay client_replay_solver; do
  sources='Quake/mathlib.c'
  if [ "$fixture" = client_replay_solver ]; then
    sources="$sources Quake/world.c"
  fi
  cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wall -Wextra -Werror \
    -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
    -ffunction-sections -fdata-sections -fsanitize=address,undefined \
    -fno-sanitize-recover=all -fno-omit-frame-pointer \
    "tests/${fixture}_fixture.c" $sources -Wl,--gc-sections \
    $(pkg-config --cflags --libs sdl3) -lm -o "/tmp/qsvr-${fixture}-asan"
  "/tmp/qsvr-${fixture}-asan" || exit 1
done
```

`client_public_preview_fixture.c` covers the public send/read/relink ordering
behind the final timing correction. It uses actual accumulation, final command,
preview, serialization and journal functions. A wrapper captures commands and
forwards to the real sender; demo suppression avoids socket output. Its external
device sampler supplies held/released joystick and mouse input. It reproduces
the reader's two clock assignments rather than linking the whole renderer, so
this is not live public-server or physical joystick qualification.

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
  -ffunction-sections -fdata-sections -fsanitize=address,undefined \
  -fno-sanitize-recover=all -fno-omit-frame-pointer \
  tests/client_public_preview_fixture.c Quake/cl_input.c Quake/common.c Quake/mathlib.c \
  -Wl,--gc-sections -Wl,--wrap=CL_SendMove -Wl,--wrap=CL_Disconnect \
  $(pkg-config --cflags --libs sdl3) -lm -o /tmp/qsvr-client-public-preview-asan
/tmp/qsvr-client-public-preview-asan
```

## OpenXR controller button input

`vr_input_fixture.c` exercises the production adapter with a recording key sink:
profile mappings, shared Index-pad ownership, trigger hysteresis, role/focus
changes, neutral rearming, menu activation versus binding capture, native modal
grabs, callback invalidation, finite-axis handling, and zero/excessive deadzones.
It also checks prepared roomscale deltas, repeated-frame deduplication,
nonconsuming preview, focus loss, outlier rejection and angle locks. A focused
render-frame case checks pinned private muzzle/hand preparation, roomscale
subtraction, both handedness mappings, repeated previews, and missing-pose
gates; it does not qualify server weapon use. The other input cases intentionally
have `should_render == false`: focused actions remain usable independently of
visibility. The key sink does not qualify native binding
execution or a physical controller.

```sh
cc -std=gnu11 -DUSE_SDL3 -Wall -Wextra -Werror \
  -Wno-missing-field-initializers -Wno-unused-parameter \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -ffunction-sections -fdata-sections tests/vr_input_fixture.c \
  Quake/vr_input.c Quake/vr_locomotion.c Quake/mathlib.c -Wl,--gc-sections \
  $(pkg-config --cflags --libs sdl3) -lm \
  -o /tmp/qsvr-controller-input-asan
/tmp/qsvr-controller-input-asan
```

`vr_input_keys_fixture.c` links the production key-name converters and checks
that existing gamepad/alternate key codes remain unchanged and the three VR
names round-trip within the native table capacity:

```sh
for source in keys common; do
  cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE \
    -ffunction-sections -fdata-sections -Wno-unused-parameter \
    -c "Quake/$source.c" $(pkg-config --cflags sdl3) \
    -o "/tmp/qsvr-controller-$source.o" || exit 1
done
cc -std=gnu11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
  tests/vr_input_keys_fixture.c /tmp/qsvr-controller-keys.o \
  /tmp/qsvr-controller-common.o -Wl,--gc-sections -o /tmp/qsvr-controller-keys
/tmp/qsvr-controller-keys
```

The initialized native tests need GDB with Python and a diagnostic Linux binary.
Set `INPUT_TEST_ROOT` to a disposable basedir containing `id1/pak0.pak` (and
`pak1.pak` if using split stock archives), linked from licensed game data. Never
point these probes at the deployed game profile; initialization can write logs.
Set `INPUT_TEST_BINARY` to the built executable. Both desktop probes inject only
OpenXR action fields; native keys, binding commands, usercmd construction and
server gameplay remain the actual engine implementations.

```sh
QSVR_INPUT_RESULT="$INPUT_TEST_ROOT/gameplay-result.json" \
  timeout --signal=TERM 75s gdb -nx --batch \
  -x tests/vr_input_gameplay_smoke.gdb --args "$INPUT_TEST_BINARY" \
  -novr -nosound -window -width 640 -height 480 -basedir "$INPUT_TEST_ROOT" \
  +vid_vsync 0 +host_maxfps 144 +map e1m1

QSVR_INPUT_BASEDIR="$INPUT_TEST_ROOT" \
QSVR_INPUT_LIFECYCLE_RESULT="$INPUT_TEST_ROOT/lifecycle-result.json" \
  timeout --signal=TERM 120s gdb -nx --batch \
  -x tests/vr_input_lifecycle_smoke.gdb "$INPUT_TEST_BINARY"
```

Require the `QSVR_INPUT_GAMEPLAY_PASSED` and `QSVR_INPUT_LIFECYCLE_PASSED`
markers and successful JSON assertions. Gameplay must move the authoritative
player and consume ammunition; focus loss releases the actions, restoring focus
while held does not resume them, and a fresh press fires again. The lifecycle
probe exercises native alternate bindings, combined hands, role/focus changes,
held-key rebinding, clears, actual menu binding capture, submenu boundaries
and modal input-grab transitions. It kills its owned
inferior after checking rather than saving a probe configuration.

`vr_input_modal_smoke.gdb` additionally requires the isolated simulated Monado
setup described under **Local OpenXR GPU smoke**, with matching private
`XDG_RUNTIME_DIR`, configuration/data paths and `XR_RUNTIME_JSON`. It uses the
actual XR frame owner and native blocking dialog, while injecting controller
actions at the adapter boundary. It verifies confirm/cancel/timeout, held-entry
suppression, simultaneous unrelated controls, cancellation precedence, active
native ALT modifiers, fresh XR frames without host-frame advancement, and the
actual loading-screen input invalidation path. It is not physical-input, headset,
performance, or Vulkan-validation qualification.

```sh
SDL_VIDEODRIVER=x11 QSVR_MODAL_RESULT="$INPUT_TEST_ROOT/modal-result.json" \
  timeout --signal=TERM 90s gdb -nx --batch \
  -x tests/vr_input_modal_smoke.gdb --args "$INPUT_TEST_BINARY" \
  -openxr -nosound -window -width 640 -height 480 -basedir "$INPUT_TEST_ROOT" \
  +vid_vsync 0 +host_maxfps 144 +map e1m1
```

Require `QSVR_INPUT_MODAL_PASSED` and all JSON assertions. Stop only the isolated
Monado service created for the probe. No headset runtime settings are changed.


## OpenXR analog locomotion and turning

`vr_locomotion_fixture.c` links the ported arithmetic with actual native math.
It checks head/offhand projection, near-vertical pitch and roll, RAW vertical
movement, singular/invalid inputs, controller gun-angle matrix composition,
aim-space calibration rotation, and body-relative hand grip geometry including
yaw, height, overflow and output aliasing. Muzzle tests cover donor model-space
left-hand reflection, including a pitched/rolled wrist that cannot be reduced
to a world-axis sign change.

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wno-unused-parameter \
  -ffunction-sections -fdata-sections tests/vr_locomotion_fixture.c \
  Quake/vr_locomotion.c Quake/mathlib.c -Wl,--gc-sections \
  $(pkg-config --cflags --libs sdl3) -lm -o /tmp/qsvr-locomotion-math
/tmp/qsvr-locomotion-math
```

The existing controller fixture also checks the production adapter's derived
pending-command ownership, all three movement modes, nonconsuming assembly,
pose/authority loss and neutral rearm, merged wire limits, snap hold/reversal,
smooth timing and queued 180 turning. Its mapped-pose and turn functions are
fixture boundaries; it does not qualify actual view/runtime or server behavior.
The camera fixture separately exercises the actual view owner across all seven
aim modes, an origin rebase between turn acceptance and view resolution,
single-commit turning and absolute/relative authority precedence. Its motion
invalidation hook is observed, not a substitute for native input integration.


`vr_locomotion_smoke.gdb` runs an initialized stock-map client through actual
OpenXR frame handling, native commands and server movement. The isolated runtime
must expose a focused HMD. Synthetic controller poses/actions are supplied at the
completed-frame boundary; no physical controllers are required for this probe.
The probe reads the adjusted stereo camera before native restoration. It checks
all movement modes/handedness, focus rearm, run scaling, snap/smooth/180 turning,
repeated previews and actual no-send/catch-up schedules using native cvars.
It does not inject live peer authority/rebase messages or qualify hardware.

With the same private runtime/manifest/XDG environment as the modal probe:

```sh
QSVR_LOCOMOTION_RESULT="$INPUT_TEST_ROOT/locomotion-result.json" \
  timeout --signal=TERM 180s gdb -nx --batch \
  -x tests/vr_locomotion_smoke.gdb --args "$INPUT_TEST_BINARY" \
  -openxr -nosound -window -width 640 -height 480 \
  -basedir "$INPUT_TEST_ROOT" +map e1m1
```

Use only a disposable profile linked to the licensed Straight assets. This probe
changes ordinary cvars/bindings in that isolated process and stops its client on
exit. The caller owns starting/stopping the separate simulated runtime service.
