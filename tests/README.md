# Migration boundary fixtures

## Avatar identity and protocol parser

`avatar_retarget_fixture.c` ports the inherited semantic profiles and CPU
retarget checks to the donor `md5_skeleton_view_t`. It covers all built-in
profile maps, global palette transport, presentation transforms, humanoid
limb lengths (including a 256-joint descendant branch), calibrated desktop
support-hand motion, and the checked bridge from an
`R_VRIKBuildRangerPalette` output. The bridge test uses a synthetic
solved palette; it does not run the VRIK solver or render a model. Run it on
Linux with:

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-sign-compare \
  -Wno-missing-field-initializers -IQuake \
  tests/avatar_retarget_fixture.c Quake/r_avatar.c \
  $(pkg-config --cflags --libs sdl3) -lm \
  -o /tmp/avatar-retarget-fixture
/tmp/avatar-retarget-fixture
```

`avatar_lower_fallback_fixture.c` checks tracked Dog upper/lower rollback,
hip handling, and equality between a rebuilt and frame-staged presentation
context. Run it with the same compiler flags and `r_avatar.c` source as above,
replacing the fixture source and output name:

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-sign-compare \
  -Wno-missing-field-initializers -IQuake \
  tests/avatar_lower_fallback_fixture.c Quake/r_avatar.c \
  $(pkg-config --cflags --libs sdl3) -lm \
  -o /tmp/avatar-lower-fallback-fixture
/tmp/avatar-lower-fallback-fixture
```

`avatar_protocol_fixture.c` links the source master pure parser. It checks
builtin identities, canonical protocol commands, capability latching, custom
key and digest validation, and custom slot updates and clears. The client and
server now negotiate these identities; the local package registry only
resolves installed matching digests. Loading and drawing selected custom models
in Vulkan is a separate renderer integration step.

```sh
cc -std=c11 -Wall -Wextra -Werror \
  tests/avatar_protocol_fixture.c Quake/player_avatar.c \
  -o /tmp/avatar-protocol-fixture
/tmp/avatar-protocol-fixture
```

`vr_akimbo_stale_fixture.c` includes the production input implementation and
checks stale QBJ3 pair admission in `VR_InputApplyPending`: a pair with the
default invalid producer identity clears the private pose and akimbo payload,
suppresses controller-aim attack, and preserves pending movement across repeat
application. It does not exercise pair production or command codecs.

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-unused-function -Wno-sign-compare \
  -Wno-missing-field-initializers -ffunction-sections -fdata-sections \
  -fsanitize=address,undefined -fno-sanitize-recover=all \
  -fno-omit-frame-pointer -IQuake tests/vr_akimbo_stale_fixture.c \
  -Wl,--gc-sections $(pkg-config --cflags --libs sdl3) -lm \
  -o /tmp/quakespasm-vr-akimbo-stale-fixture
/tmp/quakespasm-vr-akimbo-stale-fixture
```

`vr_akimbo_crosshair_fixture.c` runs the production crosshair-ray selector with
prepared left/right pair anchors. It checks two distinct rays, dominant-hand
selection and handedness, single-ray fallback, and no ray on an invalid hand or
non-rendering frame. The per-hand positions and angles are stubbed; this does
not verify model alignment or headset images.

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-unused-function -Wno-sign-compare \
  -Wno-missing-field-initializers -ffunction-sections -fdata-sections \
  -fsanitize=address,undefined -fno-omit-frame-pointer -IQuake \
  tests/vr_akimbo_crosshair_fixture.c -Wl,--gc-sections \
  $(pkg-config --cflags --libs sdl3) -lm \
  -o /tmp/qsvr-akimbo-crosshair-fixture
ASAN_OPTIONS=detect_leaks=0 /tmp/qsvr-akimbo-crosshair-fixture
```

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
muzzle edits, shared solo/co-op offsets despite legacy multiplayer fields,
invalid and missing
profiles, reset behavior, and all 99 cvar slots. A full-slot insertion failure
must leave the existing calibration and all free slots unchanged. It also calls
the production projectile source helper to check the pitched 8-unit forward
and 16-unit world-up default, grenade origin, schema right/up/forward offsets,
optional view height, self-origin overrides, and independence from legacy
multiplayer muzzle fields.

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
four supported pose formats, skin forwarding, shared classic and enhanced
muzzles in solo/co-op, raw-grip fallback for missing profiles, and rejection
of nonfinite authored calibration values.

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
real schema parser, including all 16 inherited classic and eight enhanced
stock fallbacks, missing-file behavior, field-wise file overrides, implicit
held-to-muzzle Z updates and explicit muzzle independence, later reloads,
all ten Enyo classic held/muzzle defaults, Enyo identity-only schema with a
parsed but unused global MP overlay, file freeing, and safe built-in retention
after malformed input. The file
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

`vr_weapon_calibration_save_fixture.c` exercises the production token-span
writer on compact mixed-format blocks, trailing comments, absent classic
muzzles, removal of per-weapon legacy MP offsets from updated blocks, and
new-entry placement before global offsets:

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
  -ffunction-sections -fdata-sections -IQuake \
  tests/vr_weapon_calibration_save_fixture.c Quake/vr_weapon_schema.c \
  Quake/common.c -Wl,--gc-sections $(pkg-config --cflags --libs sdl3) -lm \
  -o /tmp/quakespasm-vr-weapon-calibration-save-fixture
/tmp/quakespasm-vr-weapon-calibration-save-fixture
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
with Vulkan command spies. Across ordinary transparency, WBOIT, MBOIT, desktop
and stereo, MSAA off/4x, and SSAO on/off, it checks that a failed acquisition
leaves scene/prepared commands intact and executes neither the UI/presentation
framebuffer nor screenshot readback. The failed-acquisition cases deliberately
have no UI framebuffer and an invalid image index. When SSAO is enabled, it checks
that both SSAO record steps are scheduled in desktop and stereo. Successful
acquisition still executes both UI subpasses and readback. This does not validate
actual GPU execution or task scheduling.

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
transparency modes. It checks view masks and final output layouts, and checks
that the shared frame graph schedules both SSAO record steps in either mode
when SSAO is enabled. These are API-shape and scheduling checks, not driver
validation.

`vr_openxr_math_fixture.c` compares the production relative clip correction with
an independently calculated direct eye projection across asymmetric/canted eyes,
head rotation, translation, IPD, reversed depth and invalid inputs. It also
checks hidden-area vertices at view-space Z=-1 against each eye's asymmetric
Vulkan projection, including inverted Y and invalid inputs. It checks one
multiview vertex stream with unequal per-eye triangle counts and degenerate
padding.

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

## Renderer timing overlay

Set `scr_speeds 3` to show CPU, GPU and wait times while keeping indirect
rendering eligible under its usual conditions. This mode displays no draw
counts. GPU time is delayed until its Vulkan fence slot is reused, so it is not
a matched-frame pair with the CPU and wait measurements.

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
that task rendering is effective and confirms the stereo SSAO compute path runs
with `r_ssao 1`. This matrix therefore requires the GPU's SSAO subgroup and
extended storage-image features. The final probes change worldscale and floor
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

## QBJ3 native melee callbacks

The headless native melee callback fixture is available separately:

```sh
tests/vr_qbj3_melee_outcome.sh
```

It uses `build-debug/vkquake`, GDB and isolated links to the installed id1/QBJ3
assets. It covers wrench/berserk native hit leaves, whiffs, recovery timing,
follow-up hits without a repeated prelude, subtype/expiry rejection, pending
native attacks and desktop-command rejection. The fixture supplies accepted
contacts directly; it does not prove stroke authorization, distinct-victim
limits, contact sweeps or paired rendering. It requires permission to trace
the child process with GDB.

## QBJ3 accepted wrench contact and two-target sweep

```sh
tests/vr_qbj3_contact_runtime.sh
```

The headless installed-QBJ3 fixture submits adjacent accepted contact commands,
queries real server sweeps, and checks native wrench damage on two distinct
victims while a third remains unharmed. It also checks stale, wrong-model,
disabled-policy, and desktop fallback. Explicit fault injection at native
leaf return covers owner relocation, continuity reset, death and subtype
change, including stopping the other fist before it can run. It calls the
accepted contact processor directly, so it does not cover network packet queue
drainage or full fist gameplay.
Its Bash and embedded Python syntax pass; GDB execution remains pending because
the current sandbox denies ptrace. The authored target placement may need
adjustment when a runtime preflight first runs against the installed map.

## QBJ3 wrench and Enyo katana transforms

`vr_qbj3_wrench_transform_fixture.c` links the production alias matrix,
entity rotation, held-mesh recipes, locomotion adapter and native math. It
compares transformed geometry against an independent Rodrigues rotation for
both hands, multiple pitched/rolled wrists and nonzero gun pitch. It checks controller-centered
grip placement, left-authored reflection/winding, source-offset independence,
scale/global height, output aliasing and rejected invalid values. The Enyo
cases verify source offsets, right-authored reflection/winding, no wrench
centering/roll, and game/source recipe isolation. Session and
calibration inputs are stubbed; it does not exercise model loading, collision
traces, Vulkan drawing or contact submission.

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
  -ffunction-sections -fdata-sections -IQuake \
  tests/vr_qbj3_wrench_transform_fixture.c Quake/r_alias.c Quake/gl_rmain.c \
  Quake/vr_locomotion.c Quake/mathlib.c Quake/gl_model.c Quake/common.c -Wl,--gc-sections \
  $(pkg-config --cflags --libs sdl3) -lm -o /tmp/qsvr-qbj3-wrench-transform
/tmp/qsvr-qbj3-wrench-transform
```

## Enyo katana native readiness and outcomes

`vr_enyo_melee_state_fixture.c` includes the production server implementation
and loads the installed Enyo original-index function table. The test executes
the program/hash/ABI gates and checks pending and overdue sword attacks,
harmless native hit aftermath, stale unscheduled thinks, invalid times,
QBJ3's stricter terminal-draw rule, and retirement of shared stroke state.
It stubs QC string/function lookup; it does not execute the VM or damage.
The runner extracts assets only into a disposable directory and uses
ASan/UBSan with leak detection disabled.

```sh
bash tests/vr_enyo_melee_state.sh
```

`vr_enyo_melee_outcome.gdb` exercises the native `hitsword` leaf, whiff recovery,
switch blocking, retained hit aftermath, follow-up behavior, changed-weapon
and desktop rejection, and cleanup after an injected owner death. It uses the
shared QBJ3 outcome API with an explicit integer subtype. It does not cover
the network queue or complete physical contact acquisition. Its execution
remains pending under the recorded ptrace restriction; syntax checking is
not runtime proof. Where ptrace is available:

```sh
bash tests/vr_enyo_melee_outcome.sh
```

`vr_enyo_contact_runtime.gdb` drives the server command queue drain, preflights
three reachable victims with real server sweeps, and checks two native katana
hits, preserved aftermath/recovery, the third-victim cap, trigger suppression,
duplicate rejection and changed-weapon invalidation. Its geometry is injected
at the command boundary; it does not prove client model preparation, packet
decoding or headset presentation. It also remains unexecuted under the
recorded ptrace restriction. Where ptrace is available:

```sh
bash tests/vr_enyo_contact_runtime.sh
```

All three Enyo runners accept `QSVR_TEST_ASSETS` to override the installed game root.

## QBJ3 scheduled native fist poses

```sh
tests/vr_qbj3_akimbo_runtime.sh tests/vr_qbj3_fist_pose.gdb
```

Uses the same installed, hash-pinned QBJ3 assets and debug binary as the twin
nailgun runner. Checks all four native striking frames, anatomical hand source
and fan direction, stale-pair and desktop fallback, completed-command ownership
and scoped state restoration. Requires GDB/ptrace. This new fixture has passed
embedded Python syntax checking only; runtime execution is pending because the
current sandbox denies ptrace. It does not exercise physical two-target contact.

## QBJ3 client pair pipeline

`vr_qbj3_client_pipeline.gdb` runs the client through pair preparation, both
generated alias draws on the source frame, private-usercmd writing, server pose
acceptance, and the frame-15 `SV_QBJ3AkimboAim` hook. It grants nailgun ammo to
the local server in GDB and holds attack after pair preparation. A timeout or
any failed assertion is a failure. This needs a debug-symbol `build-debug/vkquake`,
GDB, a display, licensed Quake/QBJ3 data, and an isolated Monado QWERTY service.

In a service terminal, set `QBJ3_ASSET_SOURCE` to the licensed game root and
choose a fresh `XR_TEST_ROOT` (use the same root in the client terminal):

```sh
export QBJ3_ASSET_SOURCE=/path/to/licensed/game-root
export XR_TEST_ROOT=/tmp/qbj3-client-pipeline
test -f "$QBJ3_ASSET_SOURCE/id1/pak0.pak"
test -f "$QBJ3_ASSET_SOURCE/qbj3/progs.dat"
test -f "$QBJ3_ASSET_SOURCE/qbj3/maps/start.bsp"
test -f "$QBJ3_ASSET_SOURCE/qbj3/progs/v_tnailgun.mdl"
mkdir -p "$XR_TEST_ROOT"/{run,config,data,game/id1,game/qbj3/progs}
chmod 700 "$XR_TEST_ROOT/run"
for f in "$QBJ3_ASSET_SOURCE"/id1/pak*.pak; do
  [[ -f "$f" ]] && ln -s "$f" "$XR_TEST_ROOT/game/id1/${f##*/}"
done
for name in progs.dat maps sound gfx textures; do
  [[ ! -e "$QBJ3_ASSET_SOURCE/qbj3/$name" ]] || ln -s "$QBJ3_ASSET_SOURCE/qbj3/$name" "$XR_TEST_ROOT/game/qbj3/$name"
done
for f in "$QBJ3_ASSET_SOURCE"/qbj3/progs/*; do
  [[ -e "$f" ]] && ln -s "$f" "$XR_TEST_ROOT/game/qbj3/progs/${f##*/}"
done
XDG_RUNTIME_DIR="$XR_TEST_ROOT/run" XDG_CONFIG_HOME="$XR_TEST_ROOT/config" \
XDG_DATA_HOME="$XR_TEST_ROOT/data" QWERTY_ENABLE=1 XRT_DEBUG_GUI=1 \
XRT_COMPOSITOR_FORCE_XCB=1 monado-service
```

The QWERTY driver uses Monado's debug GUI; its `Qwerty System #1` Help panel
documents keyboard/mouse controls. In another terminal, use the same isolated
XDG paths and runtime manifest:

```sh
export XR_TEST_ROOT=/tmp/qbj3-client-pipeline
XR_TEST_BINARY=${XR_TEST_BINARY:-build-debug/vkquake} \
XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_monado.json \
XDG_RUNTIME_DIR="$XR_TEST_ROOT/run" XDG_CONFIG_HOME="$XR_TEST_ROOT/config" \
XDG_DATA_HOME="$XR_TEST_ROOT/data" SDL_VIDEODRIVER=x11 \
  timeout --signal=TERM 120s gdb -nx --return-child-result --batch \
  -x tests/vr_qbj3_client_pipeline.gdb --args "$XR_TEST_BINARY" \
  -basedir "$XR_TEST_ROOT/game" -game qbj3 -openxr -nosound -window \
  -width 640 -height 480 +vid_vsync 0 +host_maxfps 144 \
  +sv_qsvr_private 1 +sv_coop_autosave 0 +coop 1 +map start
```

Require exit 0 and `QBJ3_PIPELINE_PASSED`. The final breakpoint verifies
one accepted physical-hand shot path; it does not require both hands to fire.

## QBJ3 Flak wrist-roll path

`vr_qbj3_flak_roll.gdb` equips QBJ3's Flak Shotgun under simulated tracked
OpenXR, writes a 45-degree physical wrist roll into the private command, then
checks that its native `W_FireFlakShotgun` uses the rolled spread basis while
QuakeC still sees zero camera roll. It checks the firing path, not projectile
impact or damage. Use the isolated Monado/QBJ3 setup above and also mount the
installed weapon profile, which supplies Flak's calibrated muzzle:

```sh
ln -sfn "$QBJ3_ASSET_SOURCE/qbj3/vr_weapons.txt" \
  "$XR_TEST_ROOT/game/qbj3/vr_weapons.txt"
XDG_RUNTIME_DIR="$XR_TEST_ROOT/run" XDG_CONFIG_HOME="$XR_TEST_ROOT/config" \
XDG_DATA_HOME="$XR_TEST_ROOT/data" SDL_VIDEODRIVER=x11 \
XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_monado.json \
  timeout --signal=TERM 90s gdb -nx --return-child-result --batch \
  -x tests/vr_qbj3_flak_roll.gdb --args "$XR_TEST_BINARY" \
  -basedir "$XR_TEST_ROOT/game" -game qbj3 -openxr -nosound -window \
  -width 640 -height 480 +sv_qsvr_private 1 +map start \
  > "$XR_TEST_ROOT/qbj3-flak-roll.log" 2>&1
```

Require exit 0 and `FLAK_ROLL_PASSED`. A missing profile leaves the private
VR pose inactive and makes this probe time out.

## Enyo paired-SMG first-shot path

`vr_enyo_client_pipeline.gdb` drives the Enyo paired-SMG path through generated
pair readiness, both alias draws, private-usercmd writing, server pose
acceptance, and successful native Enyo makevectors, aim, and trace hooks. At
trace-hook return it requires exactly one nail to have been consumed (100 to
99). It compares each server world muzzle with body origin plus its
player-relative wire muzzle using a 0.01-unit 3D tolerance and checks that the
pair remains separated in 3D. The probe grants the local player the SMGs and
ammo in GDB, then holds attack. It stops before Enyo QC statement 15363
(`FireBullets2`), so it does not prove projectile creation, a hit, damage, or
impact. It captures one selected-hand shot and does not prove that both hands
fired or alternated. A timeout or failed assertion is a failure.

This needs a debug-symbol `build-debug/vkquake`, GDB, a display, licensed Quake
and Enyo data, and an isolated Monado QWERTY service. In a service terminal,
set `ENYO_ASSET_SOURCE` to the licensed game root and choose a fresh
`XR_TEST_ROOT` (use the same root in the client terminal):

```sh
export ENYO_ASSET_SOURCE=/path/to/licensed/game-root
export XR_TEST_ROOT=/tmp/enyo-client-pipeline
test -f "$ENYO_ASSET_SOURCE/id1/pak0.pak"
test -f "$ENYO_ASSET_SOURCE/enyo/pak0.pak"
mkdir -p "$XR_TEST_ROOT"/{run,config,data,game/id1,game/enyo}
chmod 700 "$XR_TEST_ROOT/run"
for f in "$ENYO_ASSET_SOURCE"/id1/pak*.pak; do
  [[ ! -f "$f" ]] || ln -s "$f" "$XR_TEST_ROOT/game/id1/${f##*/}"
done
for f in "$ENYO_ASSET_SOURCE"/enyo/pak*.pak; do
  [[ ! -f "$f" ]] || ln -s "$f" "$XR_TEST_ROOT/game/enyo/${f##*/}"
done
if [[ -f "$ENYO_ASSET_SOURCE/enyo/vr_weapons.txt" ]]; then
  ln -s "$ENYO_ASSET_SOURCE/enyo/vr_weapons.txt" "$XR_TEST_ROOT/game/enyo/vr_weapons.txt"
fi
XDG_RUNTIME_DIR="$XR_TEST_ROOT/run" XDG_CONFIG_HOME="$XR_TEST_ROOT/config" \
XDG_DATA_HOME="$XR_TEST_ROOT/data" QWERTY_ENABLE=1 XRT_DEBUG_GUI=1 \
XRT_COMPOSITOR_FORCE_XCB=1 monado-service
```

The QWERTY driver uses Monado's debug GUI; its `Qwerty System #1` Help panel
documents keyboard and mouse controls. In another terminal, use the same
isolated XDG paths and runtime manifest:

```sh
export XR_TEST_ROOT=/tmp/enyo-client-pipeline
XR_TEST_BINARY=${XR_TEST_BINARY:-build-debug/vkquake} \
XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_monado.json \
XDG_RUNTIME_DIR="$XR_TEST_ROOT/run" XDG_CONFIG_HOME="$XR_TEST_ROOT/config" \
XDG_DATA_HOME="$XR_TEST_ROOT/data" SDL_VIDEODRIVER=x11 \
  timeout --signal=TERM 120s gdb -nx --return-child-result --batch \
  -x tests/vr_enyo_client_pipeline.gdb --args "$XR_TEST_BINARY" \
  -basedir "$XR_TEST_ROOT/game" -game enyo -openxr -nosound -window \
  -width 640 -height 480 +vid_vsync 0 +host_maxfps 144 \
  +sv_qsvr_private 1 +sv_coop_autosave 0 +coop 1 +map start \
  > "$XR_TEST_ROOT/enyo-client-pipeline.log" 2>&1
```

Require exit 0 and `ENYO_PIPELINE_PASSED`. Keep the raw local log private; it
can contain device identifiers. Reuse or stop only the isolated Monado service
started for this probe.

## Staged shared movement solver

`pmove_migration_fixture.c` runs the transplanted PMove algorithm against the
real vkQuake hull functions in `world.c`. It covers walking/floor contact, jumping,
frozen commands, once-per-command roomscale across substeps, outlier rejection,
entity boxes, rotated brush normals, stationary/startsolid, water contents and source-equivalent raw clip leaves.
It also latches a transient fluid crossing across an explicitly timed command's
substeps, even when that command finishes dry, then clears the latch on the next
command.
It checks the selected stock-teleporter input rule in the production air-move
function: backward input is suppressed during the deadline while sideways
input remains effective; ordinary air movement remains unrestricted.
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
The roomscale wall case also distinguishes a 125 ms command from a 25 ms
command using the same small tangential displacement: clipping must use the
full command duration even when PMove subdivides the command.

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

`vr_weapon_contact_policy_smoke.gdb` injects one real `svc_stufftext` offer,
then checks the production server-command handler and shared
collision-authorization predicate before engine
initialization. It covers local-command rejection, public versus pinned-private
peers, collision and melee bits, malformed-offer revocation, and local opt-out.
It also rejects incomplete quotes and comments in a wire offer.
It does not exercise network delivery, server policy updates or a live shot.

```sh
gdb -nx --batch -x tests/vr_weapon_contact_policy_smoke.gdb --args \
  build-debug/vkquake -novr
```

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
a received non-solid sentinel must remove that collision. The same synthetic
solid also checks the production two-stage weapon resolver's finite muzzle
retraction without moving its input grip or tip. Individual and full
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
The public-protocol regression case also proves that VR-only and paired-weapon
fields cannot change desktop packet bytes, while predinfo and the shared weapon
selection field remain encoded.

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wno-unused-parameter \
  -ffunction-sections -fdata-sections tests/private_send_fixture.c \
  Quake/sv_user.c Quake/common.c Quake/mathlib.c Quake/vrik_codec.c -Wl,--gc-sections \
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
message-end commit helper. It requires a complete per-message movement-stat
receipt group and rejects a matching owner/ACK without one. It covers
accepted/stale/equal ACKs, epoch changes, standalone invalidation, repeated
owner resets including same-time interpolation preservation, removal/world
reset, owner changes, nonfinite state and logical
truncation prefixes. Its packet-loss case
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
initialization. It verifies matching owner/ACK and complete movement-stat
publication only at message end, including rejection when the stats are absent,
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
only controller pose/actions by default; the runtime HMD, native bindings,
command builder, transport and dedicated server remain live. For an optional
roomscale parity diagnostic, set
`QSVR_PINNED_VR_HEAD_RAMP_METERS_PER_ACTION` to a finite value in `(0, 0.1]`
(for example `0.005`). It adds that many metres to completed-frame HMD x on
each action sample after the first 25; the default `0` leaves the runtime HMD
unchanged. To reproduce a small horizontal slide along the other OpenXR
horizontal axis, optionally set
`QSVR_PINNED_VR_HEAD_Z_METERS_PER_ACTION` to a finite signed value with
magnitude at most `0.1` (for example `-0.005`). The script anchors completed-
frame HMD Z at its first action sample, holds it there through sample 25, then
sets it to `anchor + rate * (sample - 25)`. When set to `0`, Z stays frozen at
the anchor; when unset, runtime Z is left unchanged. OpenXR X and Z are the
horizontal axes used by roomscale input, while HMD Y is vertical and is never
edited by this diagnostic. The JSON reports whether Z is synthetic and records
the anchor/ramp; the script checks every HMD matrix component against its exact
float32 value. This changes the app's completed XR frame synthetically for the
diagnostic; it does not move the physical headset or inject usercmd/server
state, and does not establish real roomscale collision or geometry parity.
The probe requires focused stereo/private signon, the active shotgun's
ten-unit muzzle calibration, a finite relative VR attack command, a covering
server ACK and authoritative shell consumption. It writes a compact JSON
result and prints
`QSVR_PINNED_VR_PASSED`. This does not establish muzzle world origin, damage,
or visible weapon alignment.

For the **2.0 selected private WALK** path, start this branch's dedicated binary
with `+sv_qsvr_private 1 +sv_private_pmove_walk 1 +coop 1 +map e1m1` and a
separate isolated basedir. Connect without the trailing `qsvr1` selector, which
requires the inherited server layout. Use the same simulated Monado environment
and client probe above, adding
`QSVR_PINNED_VR_EXPECT_SELECTED_PREDICTION=1` and client arguments
`+host_maxfps 144 +host_phys_max_ticrate 30`. The 30 Hz send/physics rate leaves
rendered frames between sends under GDB. The opt-in probe binds a synthetic
OpenXR left-primary action to `+forward` during firing and requires a VR-active
serialized command, coherent selected owner/ACK, successful production replay,
displayed-owner movement while the authoritative state and command count stay
fixed, and subsequent settling. An optional
`QSVR_PINNED_VR_HEAD_RAMP_METERS_PER_ACTION=0.005` also exercises a controlled
roomscale delta. These simulated input checks do not qualify a physical
controller, headset, network loss, or general movement parity.

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
order. The raw-Gorilla replay check includes a first RESET and the disposable
preview when all sent commands are acknowledged. It also checks that an OFF
journal command clears the old planted-hand snapshot before preview and that a
raw command with no reconstructible baseline suppresses prediction.
The selected private PMove-engine dry-snapshot check discards replay after a
fluid crossing in either the journal or the unsent preview; public replay and
the separate QC-command authority remain unaffected.
`client_replay_solver_fixture.c`
instead uses the actual shared PM solver
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
changes, neutral rearming, deferred postdraw menu-panel hit testing, binding
capture, native modal grabs, callback invalidation, finite-axis handling, and
zero/excessive deadzones. It checks clickable-panel mouse selection, Enter
fallback, held-trigger stability across hover changes, matching release events,
and OpenXR menu haptics. It also covers Escape opening the menu, handedness, the
master toggle, capture and modal-grab mappings, and context/focus loss gates.
Motion coverage includes prepared roomscale deltas, repeated-frame
deduplication, nonconsuming preview, focus loss, outlier rejection and angle
locks. A focused
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

The broad input fixture's standalone link currently needs updated FBT,
weapon-menu and calibration stubs after later source ports. Its command above
is retained as a coverage target; a Linux build does not substitute for it.
The focused production-source default-binding fixture runs independently:

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-unused-function -Wno-sign-compare \
  -Wno-missing-field-initializers -ffunction-sections -fdata-sections \
  -fsanitize=address,undefined -fno-omit-frame-pointer -IQuake \
  tests/vr_default_bindings_fixture.c -Wl,--gc-sections \
  $(pkg-config --cflags --libs sdl3) -lm \
  -o /tmp/qsvr-default-bindings-fixture
ASAN_OPTIONS=detect_leaks=0 /tmp/qsvr-default-bindings-fixture
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

## Local private legacy-profile loopback smoke

`local_private_legacy_peer_smoke.gdb` observes an initialized client connected
to an isolated loopback dedicated server. It checks signon, private or public
protocol authority, prediction permission, settled authoritative movement,
shell consumption and advancing bounded move ACKs. The ordinary cases expect
prediction permission off; the selected-private opt-in case below also checks
between-send replay and settling. This is not a VR-input or physical-damage test.

Give the client and server separate disposable basedirs. In each, link the
canonical Straight `id1/pak0.pak` and the read-only `id1/vr_weapons.txt`:

```sh
STRAIGHT_ID1=/path/to/Straight/id1
CLIENT_PROFILE=/tmp/qsvr-local-client
SERVER_PROFILE=/tmp/qsvr-local-server
for profile in "$CLIENT_PROFILE" "$SERVER_PROFILE"; do
  mkdir -p "$profile/id1"
  ln -s "$STRAIGHT_ID1/pak0.pak" "$profile/id1/pak0.pak"
  ln -s "$STRAIGHT_ID1/vr_weapons.txt" "$profile/id1/vr_weapons.txt"
done
QSVR_BINARY=/path/to/debug/vkquake
```

Start the dedicated server in its own terminal on loopback. Private mode is
opt-in; omit the cvar command for the default-off public case:

```sh
"$QSVR_BINARY" -dedicated 4 -ip 127.0.0.1 -port 28790 \
  -basedir "$SERVER_PROFILE" +sv_qsvr_private 1 +coop 1 +map e1m1
```

Run the private client case from the repository root with a GDB-enabled Linux
debug build and GDB Python support. Set `SDL_VIDEODRIVER=x11` in the command
environment if SDL's Wayland driver reports no displays in the test session:

```sh
QSVR_LOCAL_EXPECT_PRIVATE=1 \
QSVR_LOCAL_RESULT="$CLIENT_PROFILE/private-result.json" \
  timeout --signal=TERM 150s gdb -nx --batch \
  -x tests/local_private_legacy_peer_smoke.gdb --args "$QSVR_BINARY" \
  -novr -nosound -window -width 640 -height 480 -basedir "$CLIENT_PROFILE" \
  +vid_vsync 0 +host_maxfps 144 +connect 127.0.0.1:28790
```

## Selected private WALK prediction opt-in

`sv_private_pmove_walk` is a default-off, server-side cvar. With the pinned
private profile enabled, it selects only eligible stock-QC WALK owners for the
private movement trial; only that selected owner can receive prediction
permission while the server is active. Public peers and unsupported or
ineligible owners receive no permission from this opt-in. Keep the cvar off for
ordinary private/public checks above.

For this focused Linux loopback probe, use fresh client/server profiles, the
debug-symbol Linux binary, GDB with Python support, and stock `e1m1` assets. The
owner must qualify as a live, dry stock WALK/SLIDEBOX player with the pinned
stock `progs.dat`; raw Gorilla hands are accepted, while trusted Gorilla
motion, custom physics and riding a pusher are outside the trial.

After selection, the current adapter continues the same per-command owner
through water and withholds prediction permission while wet or waterjumping.
Its QuakeC/PMove water velocity handoff has a focused fixture:

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wall -Wextra \
  -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
  -ffunction-sections -fdata-sections -fsanitize=address,undefined \
  -fno-sanitize-recover=all -fno-omit-frame-pointer \
  tests/private_water_velocity_fixture.c Quake/mathlib.c -Wl,--gc-sections \
  $(pkg-config --cflags --libs sdl3) -lm -o /tmp/qsvr-private-water-asan
ASAN_OPTIONS=detect_leaks=0 /tmp/qsvr-private-water-asan
```

This checks removal of stock drag and ledge impulse, preservation of another
QuakeC force and a deliberate pause. It is not a real-map water trajectory or
mixed-peer proof; the selected water path remains experimental.

Start a fresh selected private server:

```sh
"$QSVR_BINARY" -dedicated 4 -ip 127.0.0.1 -port 28792 \
  -basedir "$SERVER_PROFILE" +sv_qsvr_private 1 \
  +sv_private_pmove_walk 1 +coop 1 +map e1m1
```

From the repository root, run the selected prediction expectation (set
`SDL_VIDEODRIVER=x11` in this command environment if SDL cannot use Wayland):

```sh
QSVR_LOCAL_EXPECT_PRIVATE=1 \
QSVR_LOCAL_EXPECT_PREDICTION=1 \
QSVR_LOCAL_RESULT="$CLIENT_PROFILE/private-prediction-result.json" \
  timeout --signal=TERM 150s gdb -nx --batch \
  -x tests/local_private_legacy_peer_smoke.gdb --args "$QSVR_BINARY" \
  -novr -nosound -window -width 640 -height 480 -basedir "$CLIENT_PROFILE" \
  +vid_vsync 0 +host_maxfps 144 +connect 127.0.0.1:28792
```

The probe drives forward and attack, requires selected-private permission,
observes successful production replay across rendered frames while ACK, sent
sequence and authoritative origin stay fixed, then requires the displayed owner
to settle within 16 units. This is a narrow loopback behavior check; it does not
establish general movement parity, packet-loss robustness, VR tracking or
physical damage. A failed selection or unsupported state is not a public
prediction fallback.

Whenever this GDB harness is run against a server started with
`+sv_private_pmove_walk 1`, set `QSVR_LOCAL_EXPECT_PREDICTION=1` on the client
command. Without it, the harness expects the default-off permission state.

For public mode, start a fresh dedicated server with its default-off
`sv_qsvr_private` and use `QSVR_LOCAL_EXPECT_PRIVATE=0` with a separate result
path. The client still offers the versioned private profile during `pext`
negotiation; the check requires public authority. The positional `qsvr1`
argument is reserved for a server using the unmarked inherited legacy layout
and must not be supplied to this test.

To assert that private PMove stats stay absent for a public peer, set
`QSVR_LOCAL_ASSERT_PUBLIC_MOVE_STATS_OFF=1` on that public client run. After the
settled movement pair, it requires the valid bit in `cl.stats[225]` to be clear
and the gravity, max speed, jump speed and step height slots to remain zero. The
JSON result records those flags and float values under
`public_movement_stats`; success prints `QSVR_LOCAL_PUBLIC_MOVE_STATS_OFF_PASSED`.
For example, with the fresh default-off public server running:

```sh
QSVR_LOCAL_EXPECT_PRIVATE=0 \
QSVR_LOCAL_ASSERT_PUBLIC_MOVE_STATS_OFF=1 \
QSVR_LOCAL_RESULT="$CLIENT_PROFILE/public-move-stats-result.json" \
  timeout --signal=TERM 150s gdb -nx --batch \
  -x tests/local_private_legacy_peer_smoke.gdb --args "$QSVR_BINARY" \
  -novr -nosound -window -width 640 -height 480 -basedir "$CLIENT_PROFILE" \
  +vid_vsync 0 +host_maxfps 144 +connect 127.0.0.1:28790
```

For an opt-in private PMove server trial, set
`QSVR_LOCAL_ASSERT_ACTION_ACK=1` on the private client command. It records the
first attack command sequence and fails if authoritative shell consumption
appears before the completed move ACK reaches that sequence. This checks one
visible action/ACK ordering path; it does not prove every queued action, VR
weapon pose, or packet-loss case. Run a fresh selected server with
`+sv_private_pmove_walk 1` and keep the default-off server result separate.
Add `QSVR_LOCAL_ASSERT_MOVE_STATS=1` for the stock selected-owner stat probe:
it checks the received valid movement flags plus gravity, max speed, jump speed
and step height against the stock server defaults. It does not enable or prove
client replay; use the selected prediction probe above to check that behavior.
It does not check stat/ACK epoch association.
Gravity defaults to `800.0`; set `QSVR_LOCAL_EXPECT_GRAVITY` to a finite,
nonnegative float to match a live server `sv_gravity` change. For example, after
changing `sv_gravity` to `600` in the running private server console, add
`QSVR_LOCAL_EXPECT_GRAVITY=600` alongside `QSVR_LOCAL_ASSERT_MOVE_STATS=1`
on the client probe command. This setting only changes the gravity comparison;
the other expected movement stats remain at their stock values.
Add `QSVR_LOCAL_ASSERT_NONZERO_JUMP_TIMER=1` on a fresh selected `e1m1`
client run to hold jump during the movement phase and require a nonzero
received `STAT_PRIVATE_JUMP_SECS` value. The passed JSON includes
`max_jump_seen`. This checks transport of one real jump timer; it does not
establish replay parity; use the selected prediction probe above to check
between-send replay and settling.
For the same selected `e1m1` probe with zero gravity, use
`tests/private_selected_zero_gravity_server.gdb` and set
`QSVR_LOCAL_EXPECT_GRAVITY=0` on the client command; the harness accepts finite
nonnegative gravity values.
For a reproducible one-time change after the selected client's completed move
100, start a fresh `e1m1` server through
`tests/private_selected_gravity_server.gdb` with the selected-server arguments
above, then run the client with `QSVR_LOCAL_EXPECT_GRAVITY=600`,
`QSVR_LOCAL_ASSERT_MOVE_STATS=1`, `QSVR_LOCAL_ASSERT_COHERENT_OWNER=1` and
`QSVR_LOCAL_ASSERT_ACTION_ACK=1`. Require the server's
`QSVR_GRAVITY_CHANGED` marker and a passed client result. This checks a live
change and one coherent received state; it does not simulate a lost update.
Add `QSVR_LOCAL_ASSERT_COHERENT_OWNER=1` to check that the selected client's
message-end candidate names the current owner and completed ACK after receiving
the full movement-stat group. This verifies one loopback snapshot boundary;
loss, split-packet recovery and eventual replay parity still need separate
checks. This assertion alone does not require prediction; use a default-off
server for it. Against a selected server, set
`QSVR_LOCAL_EXPECT_PREDICTION=1` as described above.
Add `QSVR_LOCAL_ASSERT_PMOVE_TYPE=1` for a selected WALK server to require the
received owner `pmovetype` to be WALK (3), with its grounded bit matching the
owner's existing `EFLAGS_ONGROUND`. The default-off run expects prediction
permission off; against a selected server, set
`QSVR_LOCAL_EXPECT_PREDICTION=1` as described above. The owner projection
assertion alone does not test replay safety. Combine it with
`QSVR_LOCAL_ASSERT_NONZERO_JUMP_TIMER=1` to require that the received jump-held
bit appears during the held jump and clears after release.

To omit exactly the first nonempty unreliable server datagram sent to the
selected client after that gravity change, start a fresh selected server through
`tests/private_selected_lost_settings_server.gdb`:

```sh
gdb -nx -q -x tests/private_selected_lost_settings_server.gdb --args \
  "$QSVR_BINARY" -dedicated 4 -ip 127.0.0.1 -port 28791 \
  -basedir "$SERVER_PROFILE" +sv_qsvr_private 1 \
  +sv_private_pmove_walk 1 +coop 1 +map e1m1
```

Run the existing selected client harness against that port:

```sh
QSVR_LOCAL_EXPECT_PRIVATE=1 \
QSVR_LOCAL_RESULT="$CLIENT_PROFILE/private-lost-settings-result.json" \
QSVR_LOCAL_EXPECT_GRAVITY=600 \
QSVR_LOCAL_ASSERT_MOVE_STATS=1 \
QSVR_LOCAL_ASSERT_COHERENT_OWNER=1 \
QSVR_LOCAL_ASSERT_ACTION_ACK=1 \
  timeout --signal=TERM 150s gdb -nx --batch \
  -x tests/local_private_legacy_peer_smoke.gdb --args "$QSVR_BINARY" \
  -novr -nosound -window -width 640 -height 480 -basedir "$CLIENT_PROFILE" \
  +vid_vsync 0 +host_maxfps 144 +connect 127.0.0.1:28791
```

Require both server markers `QSVR_GRAVITY_CHANGED` and
`QSVR_DROPPED_FIRST_UNRELIABLE`, plus `QSVR_LOCAL_PRIVATE_PASSED` and the
passed JSON result. The client checks the post-drop settled movement settings,
a coherent owner/ACK snapshot, and attack-effect ACK ordering. This shows the
state is received after one deliberately omitted datagram. The GDB hook skips
the call to the transport driver, so it does not model loss after transport
sequence allocation. It does not count multiple repeated settings packets,
test arbitrary loss patterns, or prove prediction/replay parity.

To exercise an actual selected-private continuation, start a fresh server on
stock `start` through `tests/private_selected_split_server.gdb` (interactive
GDB, debug symbols required):

```sh
gdb -nx -q -x tests/private_selected_split_server.gdb --args \
  "$QSVR_BINARY" -dedicated 4 -ip 127.0.0.1 -port 28798 \
  -basedir "$SERVER_PROFILE" +sv_qsvr_private 1 \
  +sv_private_pmove_walk 1 +coop 1 +map start
```

The wrapper limits only the selected client's datagram to 220 bytes and marks
its visible non-owner entities for one reset. Require both
`QSVR_FORCED_PENDING` and `QSVR_CONTINUATION_SEEN` in the server terminal. Run
the private client command above against port 28798 with
`QSVR_LOCAL_ASSERT_MOVE_STATS=1` and
`QSVR_LOCAL_ASSERT_COHERENT_OWNER=1`; require a passed result, a valid matching
owner/ACK candidate, movement and shell consumption. This exercises one
controlled split without injecting packet loss or changing production policy.

For the private-to-public map-switch case, remove any stale readiness file,
then add `QSVR_LOCAL_MAP_READY="$CLIENT_PROFILE/map-ready"` to the private
client command above. Only after that file appears, enter these as two separate
console inputs in the server terminal, in order:

```text
sv_qsvr_private 0
changelevel e1m2
```

The result requires a changed world, completed signon and public authority.
The debugger may terminate before the server clears the old client slot, so
use a fresh dedicated server for each run or allow enough client slots for
stranded slots to expire. These are local client/server checks only; they do
not establish headset behavior, physical damage or general prediction
correctness.

## Selected-private shadow replay comparison

Use the separate client and server profiles prepared above. Build the trace
variant from the repository root; the macro adds diagnostic output only when
`QSVR_SHADOW_TRACE_FILE` is set. Use fresh capture paths for each run.

```sh
meson setup /tmp/qsvr-shadow-build -Dbuildtype=debugoptimized \
  -Dc_args=-DQSVR_SHADOW_TRACE
ninja -C /tmp/qsvr-shadow-build vkquake
QSVR_BINARY=/tmp/qsvr-shadow-build/vkquake
```

Save this dedicated-server GDB script as `/tmp/qsvr-shadow-server.gdb`:

```gdb
set pagination off
set confirm off
set debuginfod enabled off
set breakpoint pending on
set logging file /tmp/qsvr-shadow-server.log
set logging enabled on
break Quake/sv_phys.c:2888 if client->private_pmove_walk_selected && client->private_completed_move >= 2 && client->private_completed_move <= 300
commands
 silent
 printf "AUTH seq=%d x=%.9g y=%.9g z=%.9g vx=%.9g vy=%.9g vz=%.9g flags=%d jump=%.9g\n", client->private_completed_move, ent->v.origin[0], ent->v.origin[1], ent->v.origin[2], ent->v.velocity[0], ent->v.velocity[1], ent->v.velocity[2], (int)ent->v.flags, client->private_pmove_jump_secs
 continue
end
run
```

Run the server in one terminal and the client in another:

```sh
gdb -nx -q -x /tmp/qsvr-shadow-server.gdb --args "$QSVR_BINARY" \
  -dedicated 4 -ip 127.0.0.1 -port 28799 -basedir "$SERVER_PROFILE" \
  +sv_qsvr_private 1 +sv_private_pmove_walk 1 +coop 1 +map e1m1
```

```sh
SDL_VIDEODRIVER=x11 QSVR_SHADOW_TRACE_FILE=/tmp/qsvr-shadow-client.jsonl \
QSVR_LOCAL_EXPECT_PRIVATE=1 QSVR_LOCAL_RESULT=/tmp/qsvr-shadow-result.json \
QSVR_LOCAL_ASSERT_COHERENT_OWNER=1 QSVR_LOCAL_ASSERT_PMOVE_TYPE=1 \
QSVR_LOCAL_ASSERT_NONZERO_JUMP_TIMER=1 \
  timeout --signal=TERM 150s gdb -nx --batch \
  -x tests/local_private_legacy_peer_smoke.gdb --args "$QSVR_BINARY" \
  -novr -nosound -window -width 640 -height 480 -basedir "$CLIENT_PROFILE" \
  +vid_vsync 0 +host_maxfps 144 +connect 127.0.0.1:28799
python3 tests/private_shadow_compare.py --require-jump \
  /tmp/qsvr-shadow-client.jsonl /tmp/qsvr-shadow-server.log
```

The comparator requires at least 100 command-matched pairs, 30 moving pairs,
successful shadow results, finite values, matching ground state and an earlier
ACK for each target. Default limits are 0.05 units for position, 0.25 units/s
for velocity and 0.001 seconds for the jump timer. A stock-WALK desktop
loopback does not qualify latency correction, moving colliders or VR roomscale.

## Dwell paired-axe server runtime

These Linux headless fixtures use `build-debug/vkquake`, GDB, and the installed
licensed Quake/Dwell 2.2 assets under `quakespasm_straight`. The launcher makes
an isolated temporary game tree and does not load the installed user's config.

```sh
tests/vr_dwell_runtime.sh
tests/vr_dwell_runtime.sh tests/vr_dwell_physical_outcome.gdb
tests/vr_dwell_runtime.sh tests/vr_dwell_contact_runtime.gdb
```

The default policy fixture checks explicit enable/disable and exact-program
rejection. The outcome fixture executes native whiff and synthetic accepted-hit
callbacks, cooldown rejection, pending native-think exclusion and float-clock
expiry, plus native haste and teammate shielding. The contact fixture uses
linked targets and real edge sweeps, checks
simultaneous hands, reversals, pending-native continuity, deferred body pose,
anatomical native strike selection with zero movement duration, ordinary
physics queue draining, held-trigger suppression and desktop native attacks.
It reproduced the pending-native continuity failure before its fix. The
integrated held-trigger assertion clears cooldown so native cooldown cannot
mask broken engine suppression; desktop input remains the scheduling control.

The direct outcome fixture bypasses admission; the contact fixture exercises
admission and ordinary physics separately. The private WALK trial remains
stock-program-only; testing the shared maintenance pose helper does not widen
that admission. These fixtures do not qualify tracked rendering, alignment,
headset feel, Windows or ARM.

## Stock axe server regression

```sh
tests/vr_stock_axe_runtime.sh
```

This headless fixture uses the same debug build and an isolated tree of the
installed id1 PAKs. It checks the exact stock-program gate, explicit immersive
melee enablement, queued edge contact through ordinary server physics, native
damage/cooldown, and consumed-stroke continuity. The held-trigger check clears
cooldown before verifying suppression; switching to desktop input then confirms
that native attacks still schedule. It covers shared server behavior changed
by the Dwell integration, not headset tracking or rendering.

## Alkaline/LimJam axe program admission

`vr_alk_program_fixture.c` calls the production axe descriptor gate against the
installed Alkaline and LimJam `progs.dat` images. It checks the loader's CRC16,
SHA-256, packed function layouts, and the `W_FireAxe` trace-call site. Mutating
the hash or trace opcode must close admission. The script extracts read-only PAK
contents into a disposable test directory; set `QSVR_TEST_ASSETS` if the local
game root differs.

```sh
bash tests/vr_alk_program_fixture.sh
```

This is a program-identity fixture. It does not execute a physical sweep,
QuakeC damage, controller input, or headset presentation.

`vr_alk_calibration_fixture.c` extends the production calibration reload harness.
It checks that only the Alkaline and LimJam game roots receive the axe muzzle
fallback, authored schema entries override it, and a clean reload restores it.

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
  -ffunction-sections -fdata-sections -fsanitize=address,undefined \
  -fno-sanitize-recover=all -fno-omit-frame-pointer -IQuake \
  tests/vr_alk_calibration_fixture.c Quake/vr_weapon_calibration.c \
  Quake/vr_weapon_schema.c Quake/common.c -Wl,--gc-sections \
  -Wl,--wrap=COM_LoadFile $(pkg-config --cflags --libs sdl3) -lm \
  -o /tmp/qsvr-alk-calibration-fixture
ASAN_OPTIONS=detect_leaks=0 /tmp/qsvr-alk-calibration-fixture
```
