# Migration boundary fixtures

## Explicit OpenXR session recovery

`openxr_session_recovery_fixture.cpp` reuses the Vulkan boundary fixture's
helpers and runs the actual backend creation/session/frame owners with fake
runtime and Vulkan dispatch. It checks session loss/EXITING without automatic
restart, explicit recovery, loss during wait/release/end, healthy detach with
changed hardware, stale session events, API/GPU/system rejection, failed session
destruction, fresh polling failure after either retained stop, instance
escalation, retirement order and queue-lock balance.

`openxr_enable_fixture.c` includes the actual renderer source and exercises its
command, frame-transition, attachment and retirement owners. Repeated desktop
iterations after EXITING cannot attach; a fresh command schedules re-enable.
Retirement restores desktop dimensions and clears the old frame/reference.
Ordinary disable invokes input release. Runtime attachment/eligibility and
camera/input/destructor calls are spies; render resources are empty and the
device is prepared idle. It submits no GPU work. Separate camera/input-helper
fixtures cover reference preparation, held-key release and neutral gates; this
does not prove complete renderer/input/runtime integration or a live headset.

```sh
c++ -std=c++14 -DUSE_SDL3 -Wall -Wextra -Werror \
  -Wno-missing-field-initializers tests/openxr_session_recovery_fixture.cpp \
  $(pkg-config --cflags --libs sdl3) -o /tmp/qsvr-openxr-session-recovery
/tmp/qsvr-openxr-session-recovery
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wno-unused-parameter \
  -ffunction-sections -fdata-sections tests/openxr_enable_fixture.c \
  -Wl,--gc-sections $(pkg-config --cflags --libs sdl3) -lvulkan -lm \
  -o /tmp/qsvr-openxr-enable
/tmp/qsvr-openxr-enable
```

Require `OPENXR_SESSION_RECOVERY_PASSED` and `OPENXR_ENABLE_PASSED`.
See the [plan and recorded review](../docs/openxr-session-recovery-2.0-plan.md)
for retained-binding boundaries and broader recovery work.

## Local private movement and restored identities

`local_load_native_fixture.c` uses actual paired loopback transport for the
local player: client offer, server initialization and spawn/begin, move receipt,
`Host_ServerFrame`, full server datagrams and the client parser. Baselines and
renderer resources use the existing native-fixture preparation; this does not
run graphical serverinfo loading or headset input. Player skin uploads and the
two graphical particle cleanup calls are test boundaries. The second saved
identity uses the existing synthetic negotiation endpoint and real named spawn.
The actual `CL_ClearState` owner runs before resource preparation and its old
history is inspected before preparation can overwrite it.

```sh
make -C Quake -f ../tests/negotiation_native.make \
  negotiation-native-fixture USE_SDL3=1 -j4 \
  NEGOTIATION_FIXTURE=/tmp/qsvr-local-load-native-fixture \
  NEGOTIATION_SOURCE=../tests/local_load_native_fixture.c \
  NEGOTIATION_EXTRA_LDFLAGS='-Wl,--wrap=NET_SendUnreliableMessage -Wl,--wrap=R_TranslateNewPlayerSkin -Wl,--wrap=R_ClearParticles -Wl,--wrap=PScript_ClearParticles'
python3 tests/run_local_load_native.py --basedir /path/to/licensed/game
python3 tests/run_local_load_native.py --basedir /path/to/licensed/game \
  --game /tmp/qsvr-cooperative-qc-calls1/cooperative \
  --cases local fastload autofastload pending pending-ground
```

The runner links stock packs and optionally the prepared cooperative program
into disposable writable profiles; all saves remain temporary. Require exit0
and PASS for all selected cases. Stock cases cover movement/fire/replay, no
synthetic ACK at send, matching received owner, private-disabled native movement,
public desktop movement, actual menu/pause suspension and recovery, and both
explicit private fastload and ordinary load with autofastload. Those v5 loads
preserve position/ammo while reconnect discards queued and retained old input.
The stock local case also leaves a command outstanding on the actual socket,
replays it from the received snapshot, requires visible predicted displacement,
and compares prediction with subsequent authoritative completion within0.125units.
Public fastload retains its socket/signon/sequence cursor and reaches the original
graphics cleanup boundary; graphical cleanup itself is not qualified.

Actual v7 cases restore the first named identity and move it while the second
remains pending, reject saving in that interval, restore the second identity's
position/ammo and continue first-player movement. A saved ground reference to
the absent player stays on native authority before selection. Cooperative
native/correction-only authority need not permit generic client replay.
This covers these load boundaries, not the whole reverse-order/dead-player/
late-join/save-dialect matrix or complete graphical signon and multiplayer.

## Production private negotiation

`negotiation_native_fixture.c` loads stock `e1m1` and QC through the existing
offline engine bootstrap. It executes the actual client `cmd pext` offer,
server command consumer, client initialization and serverinfo writer. The
shared production protocol-prefix reader decodes the resulting header with
live and offline contexts. The test holds socket sends; it does not execute
the remaining client world-loading serverinfo path, full signon, prediction or
VR input.

Use an isolated directory with `id1/pak0.pak` and `pak1.pak` symlinks to the
installed stock assets, then run from the `2.0` repository root:

```sh
make -C Quake -f ../tests/negotiation_native.make \
  negotiation-native-fixture USE_SDL3=1 -j4
timeout --signal=TERM 25s /tmp/qsvr-negotiation-native-fixture \
  -dedicated 3 -noudp -nosound -basedir /tmp/qsvr-negotiation-native \
  -userdir /tmp/qsvr-negotiation-native
```

Require exit 0 and `NEGOTIATION_NATIVE_PASSED`. The cases include explicit server
enable/disable, matching/absent/wrong offers, incomplete extension support,
legacy offer generation, incompatible base protocol/coordinate flags,
simultaneous private/public peer isolation, and serverinfo refresh. Negotiating
private transport never selects PMove by itself. Defaults are reported
separately. Require `NEGOTIATION_NATIVE_DEFAULT_PASSED`: before any override,
the fixture checks registered/effective `private=1 pmove=1`, then an ordinary
modern offer must select private transport with movement prediction unselected.
The subsequent matrix explicitly sets both choices. None of this closes the
connected mixed-play gate.

Also require `DEMO_SERVERDATA_NATIVE_PASSED` and `DEMO_ENTITY_NATIVE_PASSED`.
The fixture executes the actual synthetic startup writer, demo file writer/
reader and shared prefix reader for public/private cases. Header failures cover
truncation, unknown versions, incompatible tuples, duplicate/misplaced markers,
unoffered live selection and unmarked colliding private-looking flags. Rejection
does not commit a partial result.

Two actual server-produced entity packets with changed owner positions are
recorded, read and decoded for ordinary private and selected/raw-Gorilla cases.
The fixture verifies that the current server really emitted the optional body;
selected admission is injected, not proved. Playback has zero producer history
and unset live capabilities; it neither advances live ACK/replay state nor
loses a following service. This covers file/packet components, not the complete
`record`/`playdemo` world/reset/camera lifecycle. Inherited trusted-body coverage
belongs to the separate ACK fixture; the current server does not emit that flag.

An isolated negative control removing the synthetic private marker fails the
offline header assertion (exit 134). The ACK fixture also seeds a pending-resume
marker and a completable cursor: playback must leave them unchanged, while the
identical live body must invoke the resume handler and clear the marker.

`mixed_native_fixture.c` reuses this bootstrap and negotiation helper. It calls
the real server `spawn`/`begin` commands for public and private stock-QC owners,
then executes the production baseline codec, both client move senders, server
packet receipt, native physics and snapshot/entity decoding in one world.
Unreliable transport and player-skin uploads are captured; the dedicated
bootstrap has no renderer textures. The fixture explicitly enables private
transport; the separate default assertion above proves the production choice.

```sh
make -C Quake -f ../tests/negotiation_native.make \
  negotiation-native-fixture USE_SDL3=1 -j4 \
  NEGOTIATION_FIXTURE=/tmp/qsvr-mixed-native-fixture \
  NEGOTIATION_SOURCE=../tests/mixed_native_fixture.c \
  NEGOTIATION_EXTRA_LDFLAGS='-Wl,--wrap=NET_SendUnreliableMessage -Wl,--wrap=R_TranslateNewPlayerSkin'
timeout --signal=TERM 25s /tmp/qsvr-mixed-native-fixture \
  -dedicated 3 -noudp -nosound -basedir /tmp/qsvr-negotiation-native \
  -userdir /tmp/qsvr-negotiation-native
```

Require `MIXED_NATIVE_PASSED`. Synthetic private VR and public desktop commands
must move and fire both actual QC owners, publish advancing completed ACKs and
matching owner positions, and expose the other player when visible. A private
tap/impulse released by a second generated packet before the world frame must
survive until processing, then clear; redundant commands must not reaccumulate
roomscale. Selected PMove/replay remain off. Client signon/resource state is
prepared by the fixture: this is a component chain, not connected signon,
the upstream client, OpenXR action production, or full mixed gameplay.
QC `frametime` follows the production host frame. The captured transport does
not advance socket sequences. Stats and entities now pass through the production
server-message parser, including end-of-message movement snapshot commitment;
this does not qualify connected reliability or graphics. Impulse `2` checks
retention/clearing without proving a change from the already selected shotgun.

Run the same executable with `-selected` in a separate isolated profile to
exercise the reviewed stock native-state adapter:

```sh
timeout --signal=TERM 25s /tmp/qsvr-mixed-native-fixture -selected \
  -dedicated 3 -noudp -nosound -basedir /tmp/qsvr-negotiation-native \
  -userdir /tmp/qsvr-negotiation-native
```

Require `MIXED_SELECTED_NATIVE_PASSED`. Only this fixture option enables the
existing selected cvar before actual private `spawn`/`begin`; no selected bit
is injected. A public desktop peer remains native in the same real stock-QC
world while private VR commands use WALK, `fly 1`/`fly 0`, and `noclip 1`/
`noclip 0`. Native authority and prediction withholding are checked before
physics as well as afterward. Completed ACKs, mandatory owner snapshots,
complete private movement stats, actual client replay after both returns,
mode epochs, stale propagation clearing and redundant roomscale receipt are
checked through existing producers/consumers. Movement prediction remains off
by default. Command production is synthetic and client signon/resources are
prepared; this is neither connected signon nor physical XR input evidence.

Add `-arrivalgap` to the selected run for the reviewed recovery matrix:

```sh
timeout --signal=TERM 25s /tmp/qsvr-mixed-native-fixture -selected -arrivalgap \
  -dedicated 3 -noudp -nosound -basedir /tmp/qsvr-negotiation-native \
  -userdir /tmp/qsvr-negotiation-native
```

Require `MIXED_ARRIVAL_GAP_PASSED` and `MIXED_SELECTED_NATIVE_PASSED`. This
extends the actually admitted session with stale queued/late commands, the real
quiet-frame `SV_RunClients` boundary, marker-only delivery, a missing first
command, a lost completion reply, native NOCLIP/FLY recovery and full/half-range
sequence gaps. It repeats a pending low16 ACK that plausibly aliases forward
and parses AWAIT_COMPLETION before executing its queued command: neither may
advance client completion. Full sequences are seeded only in the producer;
selection still comes from real negotiation/spawn/begin. Actual `setpos` tests
both teleport/gap orderings. Stock world `T_Damage`, native corpse frames and
a marked attack execute real death/respawn; no subsequent packet must leave
an indefinite stale-input exemption. A simultaneous public peer remains native.
Completion, full message snapshots and WALK replay are checked. This is
captured delivery with prepared client resources, not connected reliability,
OpenXR action sampling, graphics or a performance measurement.

## Opaque alias instancing

`alias_batch_fixture.c` executes the production alias draw and batch functions
with a captured Vulkan dispatch. It compares per-instance uniforms and geometry
state between two immediate draws and one instanced draw; also checks 16/17
capacity, incompatible-key flushes, immediate fallbacks, context isolation and
MD3 stereo descriptor binding. Vulkan dispatch and uniform allocation are test
boundaries; this is not a framebuffer, real worker or performance test.

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
  -ffunction-sections -fdata-sections -fsanitize=address,undefined \
  -fno-sanitize-recover=all -fno-omit-frame-pointer \
  tests/alias_batch_fixture.c Quake/mathlib.c -Wl,--gc-sections \
  $(pkg-config --cflags --libs sdl3) -lm -o /tmp/qsvr-alias-batch-fixture
ASAN_OPTIONS=detect_leaks=0 /tmp/qsvr-alias-batch-fixture
```

Require `ALIAS_BATCH_COMMAND_CAPTURE_PASSED`. After the Linux Make build,
validate its actual shader outputs:

```sh
spirv-val --target-env vulkan1.0 Shaders/Compiled/Release/alias_vert.spv
spirv-val --target-env vulkan1.1 Shaders/Compiled/Release/alias_stereo_vert.spv
spirv-val --target-env vulkan1.0 Shaders/Compiled/Release/alias_frag.spv
```

The vertex array must have a 112-byte stride with flags at offset 96; the
shared fragment block reads the original record-zero prefix. Batching off
still uses the new vertex shader, so it cannot alone establish equivalence to
the pre-change shader. See `docs/migration-alias-instancing-review.md` for
the architecture decision, evidence boundaries and separate framebuffer proof.

`alias_batch_vulkan_fixture.c` adds a headless real Vulkan framebuffer comparison.
It pins the reference vertex shader before batching, compares it against the
current shader with two immediate draws and one instanced draw, and requires
identical nonempty pixels for MDL and MD3. A multiview-capable device also checks
both layers with different eye matrices. Its fixed mapped allocator is a test
boundary; this does not qualify production allocator growth, task concurrency,
MSAA, full scenes or OpenXR submission.

```sh
p=/tmp/qsvr-alias-vulkan
git show 62de7c26:Shaders/alias.vert > "$p-old.vert"
glslangValidator -V --target-env vulkan1.1 -IShaders "$p-old.vert" -o "$p-old.spv"
glslangValidator -V --target-env vulkan1.1 -IShaders Shaders/alias.vert -o "$p-new.spv"
glslangValidator -V --target-env vulkan1.1 -IShaders Shaders/alias.frag -o "$p-frag.spv"
glslangValidator -V --target-env vulkan1.1 -IShaders -DSTEREO=1 "$p-old.vert" -o "$p-old-stereo.spv"
glslangValidator -V --target-env vulkan1.1 -IShaders -DSTEREO=1 Shaders/alias.vert -o "$p-new-stereo.spv"
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -O0 -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
  -ffunction-sections -fdata-sections -IQuake \
  tests/alias_batch_vulkan_fixture.c Quake/mathlib.c -Wl,--gc-sections \
  -lvulkan -lm $(pkg-config --cflags --libs sdl3) -o "$p-fixture"
"$p-fixture" "$p-old.spv" "$p-new.spv" "$p-frag.spv" \
  "$p-old-stereo.spv" "$p-new-stereo.spv"
```

Require `ALIAS_BATCH_VULKAN_DESKTOP_PASSED` and, for stereo evidence,
`ALIAS_BATCH_VULKAN_STEREO_PASSED`. Exit 77 means no accessible Vulkan 1.1
graphics device; a stereo skip is not a stereo pass. On this sandbox the default
hardware ICD has no accessible device. Running with the existing
`VK_ICD_FILENAMES=/usr/lib/chromium/vk_swiftshader_icd.json` passes the desktop
MDL/MD3 comparisons, but skips stereo because usable multiview is unavailable.

## Custom QuakeC physics

`customphysics_fixture.c` executes the production callback adapter with the
real QuakeC interpreter. Bytecode moves a body, clears its callback, and calls
an observer while friendly-fire protection and entity retention are active.
It verifies scope restoration, absent/zero callbacks and removal of the owner.
Field lookup and entity allocation are fixture boundaries; it does not prove
native dispatcher ordering, real-map trajectories or network prediction.

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wall -Wextra \
  -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
  -ffunction-sections -fdata-sections -fsanitize=address,undefined \
  -fno-sanitize-recover=all -fno-omit-frame-pointer \
  tests/customphysics_fixture.c Quake/common.c -Wl,--gc-sections \
  $(pkg-config --cflags --libs sdl3) -lm -o /tmp/qsvr-customphysics-asan
ASAN_OPTIONS=detect_leaks=0 /tmp/qsvr-customphysics-asan
```

`customphysics_native_smoke.gdb` is the fuller production dispatcher probe.
Use an isolated asset root containing stock `id1/pak*.pak` symlinks:

```sh
timeout --signal=TERM 35s gdb -nx --return-child-result --batch \
  -x tests/customphysics_native_smoke.gdb --args Quake/vkquake \
  -dedicated 3 -noudp -nosound -basedir /tmp/qsvr-customphysics-native \
  -userdir /tmp/qsvr-customphysics-native +sv_coop_autosave 0 +map e1m1
```

Require exit 0 and `CUSTOMPHYSICS_NATIVE_PASSED`. It aliases the stock `think`
field to the extension slot and enables Loop_Init only for offline headless
initialization; it tests no packets. A no-op custom callback must bypass an
otherwise invalid native movetype and preserve scheduled Think and body
position. A removing callback must skip PostThink and completion. This probe
has **not run** in the current sandbox because `ptrace` is denied.

The same dispatcher cases now also run without GDB in
`customphysics_native_fixture.c`. It links the production engine objects with
an offline dedicated entry point and the real physics source. It loads stock
`e1m1` and its QC VM, uses real entity allocation/removal, and checks that the
custom callback bypasses native movement/Think and does not acknowledge a
removed owner. An isolated asset root is required, as above.

```sh
make -C Quake -f ../tests/customphysics_native.make \
  customphysics-native-fixture USE_SDL3=1 -j4
timeout --signal=TERM 25s /tmp/qsvr-customphysics-native-fixture \
  -dedicated 3 -noudp -nosound -basedir /tmp/qsvr-customphysics-native \
  -userdir /tmp/qsvr-customphysics-native
```

Require exit 0 and both `CUSTOMPHYSICS_NATIVE_PASSED` and
`SELECTED_THINK_NATIVE_PASSED`. The latter adds diagnostic QC callbacks to
the loaded VM and exercises the actual selected owner: two/eight-command
batches with Think rescheduling inside the world window, a not-due first
opportunity followed by PostThink scheduling, empty-queue and insufficient
credit maintenance, death during scheduled Think and during a second
PreThink with a pending Think and TOSS continuation. It checks completion,
retirement, callback counts, one-shot impulse isolation and restored frame
durations. The Loop_Init wrapper creates no UDP socket or connected peer;
this is a dispatcher/QC/real-hull software proof, not wire, headset or exact
q30 gameplay qualification.
The same run must also report `NATIVE_ZERO_FRICTION_PASSED`: real native
input processing preserves finite momentum at 99, 100 and 101 units/second
with zero friction. The shared offline bootstrap is
`native_engine_fixture.h`; it can select co-op before spawning a mod fixture.

Require `SELECTED_NATIVE_EQUIVALENCE_PASSED` and
`SELECTED_NATIVE_GUARDS_PASSED` for the stock native-state adapter. Identical
loaded player/QC/client checkpoints compare the ordinary native chain with
selected native execution for both noclip acceleration modes, FLY, and FLY
with raw Gorilla hands, using empty/two/eight-command queues. Diagnostic QC
checks PreThink/Think/PostThink order/count and world clocks, recoil decays
exactly once, and brief attack/jump/impulse plus head translation are consumed
once. A subsequent no-packet frame also matches native origin/velocity/recoil.
Checks cover completion/retirement, latest button levels, no corpse resume
exemption, and rejection of non-finite state, unsupported hull/customphysics,
and negative/misaligned/out-of-range ground offsets. `groundentity` is an integer
QC entity slot. Pure native qualification preserves the stored water category;
strict WALK execution cannot continue into `PM_NORMAL` after a native state
change. Admission is staged in this dispatcher fixture; the mixed fixture
above owns the real selection and actual stock-QC integration proof.

## Exact q30 movement comparison

`q30_movement_native_fixture.c` loads the installed SHA-pinned q30 QC and
stock `e1m1` hulls, then compares native and selected dispatch from identical
player-var/QC-global checkpoints and co-op spawn positions. Selection is
injected solely in the fixture; production admission remains stock-only.
The fixture stages the same angles/buttons/durations as the input parser.
It covers a low takeoff/release with selected maintenance, ground/air boots
and their remaining-use field, and explicitly staged ladder release/rearming
and immediate re-jump. Ladder friction is zero to isolate QC damping from
the two solvers' friction integration. It does not simulate a real ladder
trigger, world entities between steps, item pickup, network peers or gaze.

Create a fresh isolated asset root. `Q30_ASSET_SOURCE` is the licensed game
root containing `id1` and the installed `q30a1024`:

```sh
export Q30_ASSET_SOURCE=/path/to/licensed/game-root
python3 - <<'PY'
import os
from pathlib import Path
source = Path(os.environ['Q30_ASSET_SOURCE'])
root = Path('/tmp/qsvr-q30-movement-native')
(root / 'id1').mkdir(parents=True, exist_ok=True)
(root / 'q30a1024').mkdir(exist_ok=True)
for asset in (source / 'id1').glob('pak*.pak'):
    (root / 'id1' / asset.name).symlink_to(asset)
for name in ('progs.dat', 'maps', 'progs', 'sound', 'gfx', 'textures', 'particles'):
    asset = source / 'q30a1024' / name
    if asset.exists():
        (root / 'q30a1024' / name).symlink_to(asset)
PY
make -C Quake -f ../tests/customphysics_native.make \
  customphysics-native-fixture USE_SDL3=1 -j4 \
  CUSTOMPHYSICS_SOURCE=../tests/q30_movement_native_fixture.c \
  CUSTOMPHYSICS_FIXTURE=/tmp/qsvr-q30-movement-native-fixture
timeout --signal=TERM 30s /tmp/qsvr-q30-movement-native-fixture \
  -dedicated 3 -noudp -nosound -game q30a1024 \
  -basedir /tmp/qsvr-q30-movement-native \
  -userdir /tmp/qsvr-q30-movement-native
```

The strict gate requires exit 0 and `Q30_MOVEMENT_NATIVE_PASSED`; per-case
traces include trajectory, release/ground flags, boots use, ladder state and
completed command. The paired 5 ms press/release batch must also emit
`Q30_PAIRED_RELEASE_PASSED`; maintenance preserves body, flags, boots uses and
completion. A separate 1/5/16/125 ms survey reports native/selected differences.
At 5 and 16 ms, velocity/flags match with position error below 0.2 units;
the 125 ms comparison permits 0.6 units for the existing integrator difference.
The 1 ms sequence is diagnostic, **not a parity gate**: native loses ground
contact and misses the next jump, while selected retains support. The survey's
completion marker does not qualify that case. See
`docs/migration-mod-movement-review.md` for the measured results and decision.
A passing short sequence is not general q30 admission:
water, grapple, real trigger cadence, trajectories beyond these bounded cases and connected
snapshot/replay still need their own comparisons.

The ordinary-dry cadence follow-up also requires:

- `Q30_DRY_HELD_LANDING_PASSED`: 48 actual-QC 8 ms commands hold through a
  real hull landing, release and re-jump. Both owners take off twice and have
  matching velocity/release/ground flags at each command. The measured peak
  position difference is 0.659554 units and landing reconverges. The one-unit
  local displacement bound is a survey limit, not identical native integration
  or a client/server replay qualification. Native startup QC is warmed before
  the checkpoint; startup is not qualified by this case.
- Two `Q30_DRY_FIRE_BATCH_PASSED` markers (counts2/8): actual `W_WeaponFrame`
  and impulse2 consume one spawned-inventory shotgun shell. Compare selected
  command callbacks with the existing native coalesced **single-world** adapter,
  checking shells, weapon, currentammo, impulse consumption and cooldown.
  Eight sequential native worlds are not treated as the same QC clock.
- `Q30_DRY_QUIET_FIRE_IMPULSE_PASSED`: before the authored deadline, selected
  maintenance spends no additional shell. Advancing the fixture QC clock past
  the deadline permits another held-attack shot without command duration or
  another ACK. A later impulse1 selects axe once; subsequent maintenance keeps
  the weapon and shells. Zero-time QC is not assumed to be effect-free.
- `Q30_NATIVE_AUTHORED_HOLD_RELEASE_PASSED`: a staged `pausetime` predicate
  invokes actual q30 PreThink via the existing fresh native adapter. Two queued
  forward commands complete with no movement/credit; two later commands move
  after expiry. This is direct adapter/input and clock staging, not production
  classification, a real teleport trigger or native-to-predictive return proof.

Two explicit controls mutate **only the test VM's memory** after the earlier
cases, resolving named functions and checking their pinned branch/return shape.
`-negative-frame-cooldown` bypasses `W_WeaponFrame`'s return and should still
exit0: `W_FireShotgun` independently guards its own cooldown. `-negative-cooldown`
bypasses both returns and must exit nonzero at the first batch's one-shell
assertion, because repeated selected callbacks then spend multiple shells.
Licensed assets and production code remain unchanged. The ordinary run must
exit0 and print all markers. These controls isolate the observed shot-count
guarding; they do not certify arbitrary QC cadence or all weapon/ability effects.

The ordinary replay consumer follow-up requires seven
`Q30_ORDINARY_REPLAY_PASSED` markers. It compares actual server QC + selected
PMove with `PM_PlayerMoveQCReplay` using the same real hulls: held landing,
zero/changed/clamped/huge-finite height, airborne press, 5 ms low release with
repeated horizontal roomscale and initial grounded instant-stop, and 125 ms
substeps. Each command matches position/velocity within .01 units and exact
ground/release flags. The huge height remains an authored value of `1e30`, while
collection reach uses the actual pre-solver velocity limit. Server physent collection is
the replay input seam; these cases do not execute normal signon or snapshot
serialization and do not qualify steps, ledges, abilities or arbitrary triggers.

The native-classification/dispatch follow-up uses the same build/run and requires
`Q30_NATIVE_STARTUP_CLASSIFICATION_PASSED`, `Q30_TYPED_NATIVE_STATE_PASSED`,
`Q30_QC_HOLD_DEFERRED_INPUT_PASSED`, `Q30_ROOMSCALE_PROBE_RESTORATION_PASSED`,
`Q30_FRESH_WATER_DISPATCH_ORDER_PASSED` and
`Q30_RETAINED_NATIVE_HEAD_GORILLA_PASSED`. Prepared pre-begin globals exercise the
shared observational predicate; actual q30 QC then completes three native
startup frames and reaches ordinary WALK. Ability/camera/mode/hull/definition/
reference probes are explicitly staged state checks. Existing boots/ladder
cases now use fresh native dispatch when classified incompatible; they no longer
claim that those branches execute ordinary selected PMove. Maintenance assertions
apply to qualified ordinary states.

Actual PreThink zeros velocity for a staged hold after native input defers;
resume cancels that input. A contact sample first qualifies with no hold, then
fails specifically under the hold while the actual contact cursor advances and
continuity resets. The probe check preserves exact linked-list neighbors,
body/command/duration/PVS and originally unlinked membership on real BSP hulls.
The stock liquid-position finder's first-match algorithm is shared via
`native_liquid_fixture.h`. A prepared real wet position with stale dry QC values
compares selected fresh-native dispatch against actual native movement exactly,
including restored water-observation order.

An additional prepared after-first-head seam retains a 30 ms raw command, clears
credit and executes the actual native owner on the next 10 ms world frame. An
ordinary pending Gorilla sample initializes the hand solver; a paired sample
with a pre-existing relocation cutoff stays skipped while movement completes.
This qualifies cutoff/retained-sample composition, **not actual later-head
horizontal dry-to-wet traversal**. Invalid ground/current-body tests qualify the
observational validator, not a composed malformed PostThink execution. Real
roomscale/trigger traversal, before-movement callback closure and normal q30
spawn/begin/full-parser/replay remain required before admission.

The scheduled damage-target follow-up uses a separate mode of the same fixture:

```sh
timeout --signal=TERM 30s /tmp/qsvr-q30-movement-native-fixture \
  -scheduledcamera -dedicated 3 -noudp -nosound -game q30a1024 \
  -basedir /tmp/qsvr-q30-movement-native \
  -userdir /tmp/qsvr-q30-movement-native
```

Require exit0, four `Q30_SCHEDULED_CAMERA_CALLBACK_PASSED` markers (axe3, sg1,
light1 and light2), and `Q30_SCHEDULED_CAMERA_HANDOFF_PASSED`. Prepared monster
HP-target and camera properties execute actual scheduled axe, hitscan shotgun,
lightning, damage, target and camera QC on real BSP. Shotgun uses the actual
configflag bit131072. Native and selected dispatch compare body/velocity/flags/
weapon/health/ammo/cooldown/completion with nonzero analog input and Gorilla
disabled/enabled. All eight gated function identities, not-due and consumed
opportunities are checked; actual safe axe2, sg2 and run animation stay selected.
A no-command due attack uses native world time and real completed held input
from a prior safe-animation command, retaining its completed cursor; malformed
deadline/function bounds reject. It does not inject a last-command history.
The mode exits before the ordinary replay matrix, so the usual fixture run is
still required. Selection/entities are prepared; this is not normal q30
admission, an authored map traversal, device input or a complete scheduled-Think
closure proof. Camera activation explicitly uses QC coop0 because the mod ignores
that activation in co-op.

The empty-ammo follow-up uses another separate mode of that same fixture:

```sh
timeout --signal=TERM 30s /tmp/qsvr-q30-movement-native-fixture \
  -emptyammo -dedicated 3 -noudp -nosound -game q30a1024 \
  -basedir /tmp/qsvr-q30-movement-native \
  -userdir /tmp/qsvr-q30-movement-native
```

Require exit0, `Q30_EMPTY_AMMO_AUTHORED_PREFIX_PASSED`,21
`Q30_EMPTY_AMMO_CALLBACK_PASSED` markers and `Q30_EMPTY_AMMO_HANDOFF_PASSED`.
Actual QC impulse4 consumes the last nail and schedules due nail2; the first
comparison retains that authored deadline. All20 affected nail/snail/grenade/
rocket/plasma callbacks then execute actual QC fallback, native-versus-selected
body/velocity/flags/weapon/ammo/completion with analog input and configured Gorilla
on/off, actual selected launches with ammo1 and ammo10, and quiet fallback after a
real completed held-input command without advancing its cursor or losing forward/
attack levels. Quiet movement parity is not compared. The21st case lets QC
choose ordinary shotgun. Future and consumed opportunities plus safe run animation
do not gate. Raw QC is checked with selection active: strict validation initially
passes; afterward frame validation passes and weapon2 is NATIVE with strict
rejection, while ordinary shotgun remains WALK and passes. Normal and
scheduled-camera modes must still pass separately.

Inventory and selection are prepared. Later callback identities/deadlines and
quiet-frame scheduling are also prepared; only the first nail2 prefix is authored
by the preceding actual shot. These are component checks, not normal admission,
authored pickup/map traversal, real tracked-pose/contact or headset evidence.

Actual horizontal roomscale entry uses a separate mode and optional shipped map
of the same fixture and bootstrap. The default map for existing modes remains
e1m1. The asset root above exposes the installed q30 maps:

```sh
timeout --signal=TERM 30s /tmp/qsvr-q30-movement-native-fixture \
  -traversal -traversal-map 1024_tango \
  -dedicated 3 -noudp -nosound -game q30a1024 \
  -basedir /tmp/qsvr-q30-movement-native \
  -userdir /tmp/qsvr-q30-traversal-user
```

Require exit0, `Q30_ROOM_ENTRY_FOUND`,
`Q30_ROOMSCALE_LIQUID_LATER_HEAD_PASSED` and
`Q30_ROOMSCALE_LIQUID_TRAVERSAL_PASSED`. The shared finder accepts a caller-owned
ordinal with its exact sample/hull/content/restoration algorithm unchanged;
the old first-match wrapper remains. Bounded neighbors must actually be dry and
clear, and the real auxiliary sweep must enter liquid. The shipped1024_tango
BSP SHA256 is d08fe272f025fa6c30ac87609ab6c8cb6183a0351578ea9cb17eca28612098c1.
Its water/depth1 ordinal45 qualifies a48-unit crossing from136,-516,428 to
136,-564,428. e1m1, e1m2 and e1m4 searches found no qualifying dry neighbor;
failed geometry/search attempts are not suppressed or counted as passes.

The test checks speculative probe restoration and compares actual fresh native
and selected dispatch with nonzero analog input, motion/flags/health/completion,
water entry, queue retirement and zero native credit. A real selected8ms dry
prefix must match the single-head reference, retain the raw30ms crossing head
unchanged, and consume it once on the next10ms native world opportunity without
command-time credit. Final body/completion must match the reference's same QC/
world opportunities; retired history clears room delta. Starts are prepared
airborne bodies and selection is injected. This is real BSP traversal with
actual QC/physics, not grounded shoreline play, normal signon/transport/replay,
all world actors, tracked-pose validation or device qualification. Normal,
scheduled-camera and empty-ammo modes remain independent checks.

The existing negotiation fixture can also run against this isolated q30 root:

```sh
make -C Quake -f ../tests/negotiation_native.make negotiation-native-fixture \
  USE_SDL3=1 -j4 NEGOTIATION_FIXTURE=/tmp/qsvr-q30-policy-negotiation-fixture
timeout --signal=TERM 30s /tmp/qsvr-q30-policy-negotiation-fixture \
  -dedicated 3 -noudp -nosound -game q30a1024 \
  -basedir /tmp/qsvr-q30-movement-native \
  -userdir /tmp/qsvr-q30-movement-native
```

Require `Q30_POLICY_SNAPSHOT_PASSED`. Actual client pext/server mask handling
covers missing/unknown policy offers, reconnect clearing and serverinfo
preservation. The loaded q30 registry has nine customstats disjoint from223; staged
scalar/two-slot/vector collisions reject. An injected selected owner writes
complete stats and owner bytes through the production full parser/movevar
consumer, preserving height120/0/4000/1e30 and velocity limit. Qualified offered
q30 policy now permits replay; the injected writer also sends actual full health
stats before movement/owner data and checks the real replay consumer. It remains
a prepared owner, not the normal-session proof. Existing stock negotiation/demo
checks still run.
Its ordinary writer seam explicitly prepares initialized QC lifecycle and
matching skill state for the shared classifier; those assignments are not a
startup/admission claim.

Normal q30 activation uses the existing mixed-session fixture and actual
production datagram sender. Build it with the recipe near the start of this
README, then use the isolated q30 asset root created above:

```sh
timeout --signal=TERM 30s /tmp/qsvr-mixed-native-fixture \
  -q30session -dedicated 3 -noudp -nosound -game q30a1024 \
  -basedir /tmp/qsvr-q30-movement-native \
  -userdir /tmp/qsvr-q30-session-user
```

Require exit0, `Q30_SESSION_REPLAY_COMPLETION_PASSED` and `Q30_SESSION_PASSED`.
Untouched product defaults, actual client offer/server negotiation/spawn/begin,
generated input, receipt, whole-world QC/physics, completion/retirement, production
send, full snapshot parser and client replay run in one public/private world.
Selected/startup/ACK/authority fields are not assigned to manufacture success.
Before any generated input, ACK0 is correctly too new for the client; received
native startup snapshots are checked after real sequences begin. Ordinary motion
then uses QC_COMMAND, live height/limit inputs and actual jump. The public peer
must remain native, visible, and move horizontally.

A generated sequence17 is replayed while delivery is withheld, then consumed on
its actual command interval: exact completion/empty queue and received ACK17 are
required; each replay position component differs from authoritative completion
by less than .01 units, accounting for the eighth-unit velocity seed. A prepared
future pausetime executes actual native QC horizontal cancellation and expires
to ordinary replay; gravity remains native for an airborne body. Actual older
and unknown-only private policy offers attempt spawn/begin in the spare slot
and remain native. Client signon/resources, captured delivery, synthetic inputs
and the prepared hold remain explicit seams. This does not prove authored
ability pickups/triggers, connected signon, tracked-pose or hardware behavior.

The discriminating q30 publication check reuses the existing stock-liquid driver,
its imported native/session owners and shared real-BSP finder:

```sh
make -C Quake -f ../tests/negotiation_native.make \
  negotiation-native-fixture USE_SDL3=1 -j4 \
  NEGOTIATION_SOURCE=../tests/stock_liquid_native_fixture.c \
  NEGOTIATION_FIXTURE=/tmp/qsvr-q30-publication \
  NEGOTIATION_EXTRA_EXCLUDE_OBJS='sv_phys.o cl_main.o' \
  NEGOTIATION_EXTRA_LDFLAGS='-Wl,--wrap=NET_SendUnreliableMessage -Wl,--wrap=R_TranslateNewPlayerSkin -Wl,--wrap=PR_ExecuteProgram'
timeout --signal=TERM 30s /tmp/qsvr-q30-publication \
  -q30publication -defaultselection -vr \
  -dedicated 3 -noudp -nosound -game q30a1024 \
  -basedir /tmp/qsvr-q30-movement-native \
  -userdir /tmp/qsvr-q30-publication-user
```

Require exit0 and `Q30_PUBLICATION_WATER_PASSED`. An actual admitted fly command
completes natively; the actual fly0 command returns the owner to WALK with its
native-completed frame flag intact. Prepared late relocation to real water2 keeps
cached dry fields. The actual received head requires forward100 with zero
up/buttons/impulse. That input, a canonical fresh-native reference
without publication, full production publication, and normal next native dispatch
must agree on body/flags/health/water/completion/retirement; publication must not
refresh cached water before native input/PreThink. The q30 full-send fixture
requires exactly one captured packet, rather than silently parsing only the last
of multiple packets. The checkpoint restores body/globals/client, world time and
logical datagram append length, not every actor/effect stream. No authored
relocation, general effect replay or full-world restoration is claimed.

The actual q30 jump-boots lifecycle uses that same stock-liquid binary and
isolated q30 root. With the publication build recipe above, run each mode in a
separate process/user directory:

```sh
# Selected private VR input with actual normal admission.
timeout --signal=TERM 30s /tmp/qsvr-q30-publication \
  -q30boots -defaultselection -vr -dedicated 3 -noudp -nosound \
  -game q30a1024 -basedir /tmp/qsvr-q30-movement-native \
  -userdir /tmp/qsvr-q30-boots-vr-selected > /tmp/q30-boots-vr-selected.log
# Native private VR reference: omit -defaultselection.
timeout --signal=TERM 30s /tmp/qsvr-q30-publication \
  -q30boots -vr -dedicated 3 -noudp -nosound \
  -game q30a1024 -basedir /tmp/qsvr-q30-movement-native \
  -userdir /tmp/qsvr-q30-boots-vr-native > /tmp/q30-boots-vr-native.log
```

Repeat both with `-vr` omitted and distinct desktop user/log paths. Require exit0
and `Q30_BOOTS_PASSED` in all four runs. Fresh native/selected processes must each
produce112 `Q30_BOOTS_SAMPLE` rows. At the default25ms interval, compare the same
frame's ownership, charge, deadline, XYZ position/velocity and flags to1e-6
printed resolution. All four recorded runs match with zero sampled difference;
this is the controlled zero-axis private jump sequence, not arbitrary movement
or complete world/effect equivalence. Other command intervals are not qualified
by those default results.

The actual pinned `item_artifact_jumpboots` spawn precaches real assets before
the existing client resource copy. Prepared count2/cnt2/height300 and initial map
start placement compose the item; actual scheduled world setup makes it a
trigger. The existing real notarget command prevents premature pickups and is
turned off before prepared relocation of that actual artifact over the player.
Actual world contact runs `item_touch` and its nested `artifact_touch`; an
observation-only callback records the top-level contact. No direct touch call or
manual player ability grant is used. The actual timed item remains owned for81
frames, with an airborne second jump on frame4 consuming charge2→1 and producing
vertical velocity280. Installed QC expires it on frame81 at the default interval.

Selected runs require successful replay acceptance before pickup; ability
snapshots require native completion/legacy authority/no replay; actual QC expiry
must restore replay acceptance. Two additional disposable previews, before pickup
and after expiry outside the112-frame comparison, require positive duration and
more than .01 units of horizontal predicted motion. Each preserves the committed
journal, authoritative baseline, ACK and jump timers, restoring the pending
command and client clock. Require both `Q30_BOOTS_POSITIVE_PREVIEW_PASSED` markers
in selected runs; an empty-history replay alone does not qualify this claim. Received command completion and empty queue are checked against
actual generated sequences. A visible, moving public desktop peer stays native.
Captured delivery, prepared client signon/resources, prepared item/placement,
notarget and synthetic inputs remain explicit seams. This does not find an
unmodified shipped-map pickup or qualify real headset input, all ability branches,
ladder/grapple or other mods. A temporary copy with only boots eligibility removed
from Q30State fails the actual owned-state native-authority assertion (exit134);
production sources and assets are untouched by that countercheck.

Installed AD's pak2 program is byte-identical to the pinned q30 program above.
An isolated root with id1/pak0 and ad/pak0,pak1,pak2 read-only asset links also
passes the same `-q30session` and selected VR `-q30boots` recipes with `-game ad`.
The actual fixture loader/exact-program assertion checks the mounted program,
not just the pack file. The boots result matches the112 q30 samples and includes
both positive previews. No AD-specific movement implementation or new hash was
added. This qualifies e1m1 under the installed AD program; actual AD maps, older
program variants and Mjolnir remain separate scope. See the
[AD identity/reuse record](../docs/predictive-ad-identical-program-2.0-plan.md).

The shared-QC vertical reuses the stock-liquid binary above. Create a separate
isolated root `/tmp/qsvr-ad-pak0-shared-native` with read-only links to licensed
`id1/pak0.pak` and `ad/pak0.pak,pak1.pak`; **omit AD pak2**, whose mounted program
is identical to q30. Both new modes assert the actually loaded older AD program:
2345354 bytes, SHA256
`f3c4218216ea0d3b00db35eef9e72945ee6687006b3e82c37ea2ae33a0eea922`.
The assertion is test-only; production general admission has no new whitelist.

```sh
timeout --signal=TERM 30s /tmp/qsvr-q30-publication \
  -genericboots -defaultselection -vr -dedicated 3 -noudp -nosound \
  -game ad -basedir /tmp/qsvr-ad-pak0-shared-native \
  -userdir /tmp/qsvr-ad-shared-vr-selected
```

Repeat in separate user directories with `-vr` omitted, then both modes with
`-defaultselection` omitted for native references. Require exit0 and
`Q30_BOOTS_PASSED` in all four runs; the inherited marker name is shared with the
earlier driver. Selected runs also require `SHARED_QC_ABILITY_PREVIEW_PASSED`,
`SHARED_QC_PREFIX_PASSED` and `SHARED_QC_COMPOSITIONS_PASSED`. Actual QC owns
pickup, ground/air boots jumps, charge and expiry. Selected owned-state snapshots
retain ENGINE_COMPAT replay instead of a boots-specific native gate. A positive
30ms ordinary jump forecast observes upward displacement under the existing NQ
substep order; arbitrary server-QC airborne forces still require correction.

For each112-frame run, ownership, charge, deadline and health match the native
reference. The native1-unit position/velocity comparison **fails**: maximum axis
position difference3.23645 and velocity difference320, with a one-frame landing
flag difference. Modern PMove and native acceleration/gravity/snap integration
are different. These are recorded differences, not a native trajectory parity
pass or permission to widen tolerances silently. Health remains100 in this
benign case; damaging landings and sounds are not qualified.

The actual producer/receipt batch checks one world-clock QC lifecycle, actual
first axe selection, first pose at the sole PostThink, a retained later jump/
shotgun impulse, advancing completed ACKs, duplicate rejection and single
roomscale consumption. Explicit compositions after real QC/trigger dispatch
check an actual impulse clear, unfixed90-degree rotation plus5-degree PostThink
increment, living FLY continuation and positive-ACK maintenance after actual
pause/unpause. The rotation/clear/FLY writes are prepared counterexamples, not
authored map abilities. Empty physical-contact cursors do not prove physical
button callbacks.

For the separate later-roomscale boundary, link licensed q30's `maps` directory
under that isolated root's `ad/maps`, preserving older AD QC. The stock e1m1
search has no reachable dry neighbor for this bounded horizontal sweep.

```sh
timeout --signal=TERM 30s /tmp/qsvr-q30-publication \
  -sharedqcboundary -fixturemap 1024_tango -defaultselection -vr \
  -dedicated 3 -noudp -nosound -game ad \
  -basedir /tmp/qsvr-ad-pak0-shared-native \
  -userdir /tmp/qsvr-ad-shared-boundary
```

Require exit0, `SHARED_QC_DEFERRED_ROOM_PASSED` and
`SHARED_QC_BOUNDARY_PASSED`. The shared real-BSP finder constrains the sweep to
the16-unit actual receipt limit; the original q30 reference keeps its64-unit
search. Prepared airborne relocation is followed by two actually sent commands.
The first completes its actual weapon effect, originating pose and single world
tail; the later crossing stays byte-identical in the queue, fenced with native
authority. The next native world consumes it and crosses real water once.
Captured transport, prepared signon/resources/body/item and synthetic VR input
are explicit limits; this does not qualify all older AD maps or hardware.
See the [shared-QC checkpoint](../docs/predictive-movement-reuse-reassessment-2.0-plan.md).

The mixed native fixture's `-velocityseeds` option checks actual serialized
signed-short velocity boundaries through the full parser and replay gate.
Use its existing selected run with `-defaultselection -earlypause -arrivalgap
-velocityseeds`. Require `PRIVATE_VELOCITY_SNAPSHOT_PASSED`: both encoded range
endpoints replay, out-of-range authoritative seeds suppress replay, and
presentation saturation leaves authored velocity untouched. The velocities
are staged snapshots; this is not an authored high-jump traversal proof.

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
  Quake/vr_weapon_calibration.c Quake/vr_weapon_schema.c \
  Quake/vr_locomotion.c Quake/mathlib.c Quake/common.c \
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
The opt-in QuakeC jump-owner cases use the same donor floor hull to verify that
120- and 300-unit upward impulses remain airborne with held input, across
single-step and explicitly timed commands; they also check a short low takeoff
followed by button release, latch preservation, water classification, and eventual
landing. The exact-mod server adapter is a separate integration gate.
The QC support correction has direct scope checks after a real floor snap,
including a small tangential force that full-vector clipping would discard.
It excludes non-QC movement, flying, zero time, legacy ground policy, absent
support/snap, water and transient fluid contact, waterjump, ladder, Gorilla,
entity support, slopes, nonstandard gravity and rising velocity. Those
exclusions are staged states, not traversal proofs. A subsequent real airborne
probe clears the snap observation. Repeated dry support uses float coordinates
and 1/5/16/125 ms commands to check momentum and horizontal travel over 32
commands with each donor hull implementation.
The optional instant-stop cases cover default-off and desktop friction, an idle
VR stop, moving input, jump preservation, and the post-QuakeC PMove exemption.
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
The private instant-stop move flag is accepted only after the feature
capability, is masked for public and older private peers, and pauses replay
until previously sent commands are acknowledged when the rule changes.

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

## Local loopback reconnect sequencing

`loopback_sequence_fixture.c` exercises local reliable delivery, a sequence
crossing the high bit, reconnect with reused sockets, and closed-peer cleanup
through the real loopback transport. It does not replace a live local game.

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wno-unused-parameter \
  -ffunction-sections -fdata-sections -fsanitize=undefined \
  tests/loopback_sequence_fixture.c -Wl,--gc-sections \
  $(pkg-config --cflags --libs sdl3) -lm -o /tmp/quakespasm-loopback-sequence
/tmp/quakespasm-loopback-sequence
```

## Shared UDP socket and NAT rebinding

`datagram_rebind_fixture.c` drives the real virtual-client `Datagram_GetMessage`
path with a scripted UDP boundary. It covers two clients behind one IP,
cross-client packet deferral, a uniquely identified source-port change, outgoing
destination, old-port stragglers, reliable retransmit ACK routing, ambiguous
new ports, oversized packets and server-side reliable-fragment retirement,
bounded inbox eviction, socket-owner cleanup, and IPv6 scope separation. It
does not replace a live multiplayer check or measure
performance.

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wno-unused-parameter \
  -ffunction-sections -fdata-sections -fsanitize=undefined \
  tests/datagram_rebind_fixture.c -Wl,--gc-sections \
  $(pkg-config --cflags --libs sdl3) -lm -o /tmp/quakespasm-datagram-rebind
/tmp/quakespasm-datagram-rebind
```

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
The pause-source regression also prepares a zero-first-sequence latch at the
current or source-ahead epoch and executes the real sender; neither case may
queue an obsolete reliable or inline resume marker. It exercises the existing
producer, rather than substituting a second recovery implementation.

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

`private_pause_server_fixture.c` executes the selected server's production
pause transition, resume marker parser and private move reader. It
checks that queued actions and held state are discarded without completing the
ACK, a stale or malformed marker is ignored, first post-marker input is queued,
and a second pause or pre-marker relocation fences that generation. Arrival
coverage adds exact-one-second versus greater threshold, unchanged physical
state/timers/completion, idempotence, duplicate markers that cannot renew the
stall clock, terminal/alive transitions, malformed fenced moves and full
sequence/ushort epoch wrap. Terminal classification and living-state validation
are fixture seams. It does not
execute QuakeC physics callbacks or a live network connection.

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wno-unused-parameter \
  -ffunction-sections -fdata-sections tests/private_pause_server_fixture.c \
  Quake/common.c Quake/mathlib.c -Wl,--gc-sections \
  $(pkg-config --cflags --libs sdl3) -lm -o /tmp/quakespasm-private-pause
/tmp/quakespasm-private-pause
```

`private_moveack_fixture.c` includes the production ACK parser and links real
MSG readers. It checks accepted/stale/equal ACKs, 16-bit expansion, the QuakeC
command frame, epoch-triggered smoothing reset calls, Gorilla capability gates,
state sequence/generation, the pinned 4096-model limit, nonfinite state rejection,
every declared truncated prefix of the extended Gorilla payload, and queue
duplicate/overflow handling. Smoothing reset and flush calls are fixture spies;
the fixture does not execute the actual smoothing reset or network flush.
Truncated prefixes retain a larger backing array, so they check logical message
bounds rather than physically truncated allocations.
Recovery checks include lost completion at 40000, 65636 and 65638, repeats before
and after the marker, awaiting-completion metadata and same-epoch pending
regression. Client and QC completion remain frozen until confirmed execution;
contradictory pending/prediction/authoritative flags are rejected atomically.

`vr_input_continuity_fixture.c` executes the actual tracking reset and roomscale
accumulator with prepared gameplay context and mapping/UI boundaries. It checks
the first new HMD sample becomes a baseline, subsequent displacement is kept,
pending contact/Gorilla/turn work is dropped, analog neutral/snap latches are
retained for network recovery, and full tracking invalidation still gates them.
It also executes the actual GateAndReleaseAll and Neutral helpers: both held
triggers release and remain neutral-gated; a held trigger or stick is rejected
until a neutral active input sample. Key_Event is a recording sink.
It does not execute the complete held-stick/button/contact submission pipeline
or a headset runtime; the old broad input fixture's dependencies remain separate.

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wno-unused-parameter \
  -ffunction-sections -fdata-sections -fsanitize=address,undefined \
  -fno-omit-frame-pointer tests/vr_input_continuity_fixture.c Quake/mathlib.c \
  -Wl,--gc-sections $(pkg-config --cflags --libs sdl3) -lm \
  -o /tmp/qsvr-vr-input-continuity
ASAN_OPTIONS=detect_leaks=0 /tmp/qsvr-vr-input-continuity
```

Require `VR_MOTION_CONTINUITY_PASSED`. Leak scanning is disabled for this
sandbox; address and undefined-behavior instrumentation remain enabled.
The sender and ACK fixtures also check the selected resume marker, clearing of
pre-observation key edges and accumulated roomscale, fresh input after that
boundary, terminal-authority marker production, unreliable marker-before-move
ordering, and full-sequence ACK recovery across half/full wraps.

The ACK fixture additionally requires `DEMO_MOVEACK_CONSUMPTION_PASSED`.
It runs ordinary/raw-Gorilla/inherited-trusted recorded bodies with zero local
command history and unset live capabilities. Complete validated bodies preserve
all live client state and leave the following service aligned. Truncated,
nonfinite and out-of-range bodies still fail; the same raw/trusted bodies still
require capability admission during a live connection. This is codec coverage,
not proof of the current server's trusted-body production or full demo playback.

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
The ordinary QC policy also requires stat223 in the current message; a cached
limit cannot replace its receipt. Zero/large finite limits remain usable,
missing/negative/NaN/both infinities reject, and large finite height/limit floats
retain defined integer companions. Explicit float-cast-overflow instrumentation
checks conversion before admission.

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wno-unused-parameter \
  -ffunction-sections -fdata-sections -fsanitize=address,undefined,float-cast-overflow \
  -fno-sanitize-recover=all -fno-omit-frame-pointer \
  tests/private_owner_snapshot_fixture.c Quake/common.c -Wl,--gc-sections \
  $(pkg-config --cflags --libs sdl3) -lm -o /tmp/qsvr-private-owner-asan
ASAN_OPTIONS=detect_leaks=0 /tmp/qsvr-private-owner-asan
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
socket, private reconnection, local-map startup and demo playback using the
current local private-transport setting. Legacy connection opt-in and the
actual decoder are checked separately; a newly recorded private demo needs
no live offer during playback.
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
order. It verifies that private replay starts from the accepted server
waterjump timer despite a stale matching local cache entry, while public
PREDINFO retains timer propagation. The raw-Gorilla replay check includes a first RESET and the disposable
preview when all sent commands are acknowledged. It also checks that an OFF
journal command clears the old planted-hand snapshot before preview and that a
raw command with no reconstructible baseline suppresses prediction.
The selected private PMove-engine contract permits fluid crossings in both
the journal and unsent preview when the received server permission allows
replay. Withheld permission still prevents solver/preview execution. Public
replay remains unchanged. QC-command authority now requires the matching
negotiated jump policy and dispatches both history and preview to its consumer;
unsupported QC policy and native liquid crossings suppress that replay. These
PM probes complement the admitted real-map liquid driver below.
`client_replay_solver_fixture.c`
instead uses the actual shared PM solver
and donor collision functions to check empty-history, zero-duration underwater
categorization. It also checks ordinary QC repeated/zero-duration preview on
a synthetic floor, with unchanged journal, pending input and authoritative
entity baseline. A held preview after a committed jump cannot add another
impulse. Its input preview and world-entity collection are fixture seams;
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
  ASAN_OPTIONS=detect_leaks=0 "/tmp/qsvr-${fixture}-asan" || exit 1
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
shell consumption and advancing bounded move ACKs. Ordinary eligible stock
private cases expect prediction and check between-send replay/settling; public
cases expect it off. Set `QSVR_LOCAL_EXPECT_PREDICTION=0` with an explicitly
disabled/native server or an unsupported mod admission. This is not a VR-input
or physical-damage test.

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

Start the dedicated server in its own terminal on loopback. Matching private
transport and eligible stock predictive movement are enabled by default:

```sh
"$QSVR_BINARY" -dedicated 4 -ip 127.0.0.1 -port 28790 \
  -basedir "$SERVER_PROFILE" +coop 1 +map e1m1
```

Run the private client case from the repository root with a GDB-enabled Linux
debug build and GDB Python support. Set `SDL_VIDEODRIVER=x11` in the command
environment if SDL's Wayland driver reports no displays in the test session:

```sh
QSVR_LOCAL_EXPECT_PRIVATE=1 \
QSVR_LOCAL_EXPECT_PREDICTION=1 \
QSVR_LOCAL_RESULT="$CLIENT_PROFILE/private-result.json" \
  timeout --signal=TERM 150s gdb -nx --batch \
  -x tests/local_private_legacy_peer_smoke.gdb --args "$QSVR_BINARY" \
  -novr -nosound -window -width 640 -height 480 -basedir "$CLIENT_PROFILE" \
  +vid_vsync 0 +host_maxfps 144 +connect 127.0.0.1:28790
```

For a simultaneous public/private desktop pairing, use a fresh default server
and two separate disposable client profiles. Run the command above with
`QSVR_LOCAL_EXPECT_PEERS=2` on the 2.0 private client. Run the unchanged pinned
upstream vkQuake binary concurrently with the same probe, its own result/profile,
and `QSVR_LOCAL_UPSTREAM=1 QSVR_LOCAL_EXPECT_PRIVATE=0
QSVR_LOCAL_EXPECT_PEERS=2`. Both must complete movement/fire checks and observe
two named scoreboard peers. The public run requires public PREDINFO and no
selected owner type; it reports private-only permission as `null` because the
upstream struct has no such field. Do not patch its offer or force a dialect.

The pinned upstream source can be built in a disposable directory:

```sh
mkdir -p /tmp/qsvr-stage1-upstream
git -C ../vkquake archive 4bc898f29073e8aa41069f0e79e3cb5a9eb73afa | \
  tar -x -C /tmp/qsvr-stage1-upstream
make -C /tmp/qsvr-stage1-upstream/Quake USE_SDL3=1 -j4
```

That build and GDB field/type checks passed in this session. Connected probes
could not run: the dedicated binary receives `Operation not permitted` when
creating UDP sockets. Syntax/type checks and scoreboard expectations do not
prove active server peers, OpenXR input, mixed gameplay or networking. The
full public/VR and private/VR release matrix remains in the migration plan.

## Ordinary stock private prediction

`sv_private_pmove_walk` defaults to1 on the server. With the matching private
profile, ordinary eligible stock-QC WALK owners select the existing command
owner at `begin`; only a matching supported state receives replay permission.
Public peers and excluded initial programs/states retain native play. Set
`+sv_private_pmove_walk 0` on a fresh server for explicitly native comparison
checks, and `QSVR_LOCAL_EXPECT_PREDICTION=0` on its private client probe.
The [activation plan](../docs/predictive-stock-activation-2.0-plan.md) separates
this supported stock default from remaining AD/cooperative-QC stages.

For this focused Linux loopback probe, use fresh client/server profiles, the
debug-symbol Linux binary, GDB with Python support, and stock `e1m1` assets. The
owner must qualify as a live, dry stock WALK/SLIDEBOX player with the pinned
stock `progs.dat`; raw Gorilla hands are accepted. Initial loaded/local or
unsupported state/program admission stays native. Trusted authored Gorilla
motion and custom physics remain outside the selected contract. Robust
moving-brush interaction is accepted through the native carry/rollback owner;
its transient marks and retained planted palms withhold replay.

After selection, the current adapter continues the same per-command owner
through water with bounded wet/ledge replay; native holds, pause and incompatible
support states still withhold permission.
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

The extended helper also checks water/slime/lava swim-overwrite reconciliation,
vertical/horizontal residual forces, all-zero pause from a moving or stationary
baseline and ledge precedence. The bounded real-map foundation is planned in
[`predictive-stock-liquid-2.0-plan.md`](../docs/predictive-stock-liquid-2.0-plan.md):

```sh
make -C Quake -f ../tests/negotiation_native.make negotiation-native-fixture \
  USE_SDL3=1 -j4 NEGOTIATION_FIXTURE=/tmp/qsvr-stock-liquid \
  NEGOTIATION_SOURCE=../tests/stock_liquid_native_fixture.c \
  NEGOTIATION_EXTRA_EXCLUDE_OBJS='sv_phys.o cl_main.o' \
  NEGOTIATION_EXTRA_LDFLAGS='-Wl,--wrap=NET_SendUnreliableMessage -Wl,--wrap=R_TranslateNewPlayerSkin -Wl,--wrap=PR_ExecuteProgram'
/tmp/qsvr-stock-liquid -selected -dedicated 3 -noudp -nosound \
  -basedir "$SERVER_PROFILE" -userdir "$SERVER_PROFILE"
/tmp/qsvr-stock-liquid -selected -fixturemap e1m2 -requireledge \
  -dedicated 3 -noudp -nosound -basedir "$SERVER_PROFILE" -userdir "$SERVER_PROFILE"
```

Use a disposable profile containing read-only stock pak0 assets. Omit `-selected`
for native private movement; add `-vr` for prepared VR pose/duplicate-roomscale
delivery. `-fixturemsec 10` and `100` exercise short and subdivided commands.
`-fixturecase ledge` runs only the discovered ledge case. `-requireledge` fails
if the loaded geometry search cannot find one; ordinary `e1m1` runs explicitly
report that no qualifying ledge was found. Startup first samples/suppresses the
two reserved commands; this fixture does not prove pause before that startup.
The existing mixed driver now has `-selected -earlypause` for actual host
pause/`svc_setpause`/reserved-command/recovery/completion/replay proof; it can be
combined with `-arrivalgap`.

`-fixtureliquid slime -fixturemap e1m1` and `-fixtureliquid lava -fixturemap e1m6`
reuse the same real-BSP depth/swim/health driver; `-skipledge` omits ledge search
when only the liquid cases are required. The default is water. PreThink health
events include actual type/depth, time and damage deadline; they are not a
counter of nested `T_Damage` calls. Slime/native health matches the current
96-sample matrix. Lava's late deep-button difference is traced to equal event
times but depth 2/20 damage versus depth 3/30 damage under the two integrators.

The liquid driver runs actual stock admission, command codecs, QC/world physics,
completion and full snapshots. It checks selected ACK/owner/timer commitment,
press/hold/release, three depths, sink/up, duplicate roomscale and ledge
startup/termination. Selected desktop/generated-VR comparisons invoke actual
live pending replay from the preceding complete snapshot, with wire-derived
bounds for flat-water and ledge positions/tangential velocities. Desktop ledge
history-only diagnostic velocities use the same bounds. The actual zero-duration
live preview can clip upward velocity on dry grounded slopes, or terminate a
falling disposable waterjump timer before the next authoritative command;
those exceptions require the exact solver conditions, rather than a broader
tolerance. The diagnostic shadow deliberately excludes VR, so generated VR
uses actual live position/velocity and those explicit solver-rule assertions.
Positive-duration disposable previews also run and preserve the committed
journal, ACK, authoritative jump timers and owner netstate. Their pending axis
state is prepared desktop input; generated VR history is a separate check.
Received permission/ACK/selection are never staged. `-selected` explicitly
chooses the selected component policy; `-defaultselection` instead leaves both
production activation cvars untouched. Native comparison runs explicitly set
selection0, independently of the production default.

Starts use actual `kill`/`setpos` plus prepared resting velocity/support/release
flags and no unrelated spawn hold. Actual `notarget` commands exclude monster
knockback from movement comparison; environmental QC remains active. Native and
QSS-M integrators intentionally differ, including stock `225` versus donor `310`
ledge impulses. Captured delivery, prepared signon/resources/input and skipped
player-skin texture uploads do not prove connected signon, upstream client,
physical XR actions, combat/hazard prediction or performance. The transit and
callback drivers below add bounded software evidence; they do not certify
every geometry/mod/pusher or a connected upstream-client session.

The separately invoked `stock_liquid_contract_fixture.c` reuses this admitted
driver and checks command-time timer/callback ownership:

```sh
make -C Quake -f ../tests/negotiation_native.make negotiation-native-fixture \
  USE_SDL3=1 -j4 NEGOTIATION_FIXTURE=/tmp/qsvr-stock-liquid-contract \
  NEGOTIATION_SOURCE=../tests/stock_liquid_contract_fixture.c \
  NEGOTIATION_EXTRA_EXCLUDE_OBJS='sv_phys.o cl_main.o' \
  NEGOTIATION_EXTRA_LDFLAGS='-Wl,--wrap=NET_SendUnreliableMessage -Wl,--wrap=R_TranslateNewPlayerSkin -Wl,--wrap=PR_ExecuteProgram'
/tmp/qsvr-stock-liquid-contract -selected -requirecontract -roundingboundary \
  -dedicated 3 -noudp -nosound -basedir "$SERVER_PROFILE" -userdir "$SERVER_PROFILE"
```

`-requirecontract` asserts correction rather than only printing the probe.
Nominal 10/25/100-ms runs check new quiet ledge normalization, unchanged active
quiet movement/timers/ACK, real pinned teleports during active commands and
quiet frames, a wet destination's preserved hold/no reacquisition, and actual
setpos release without fake ACK progress. `-roundingboundary` prepares double
clocks at which QC float addition differs from double addition then conversion,
through actual pinned quiet and command PreThink.

Deadline-only/flag-only/velocity-only composition executes actual scheduled
pinned Think, then supplies explicitly prepared independent field outputs.
Both quiet and command-time cases check surviving ownership. A later-world
scheduled Think composition links the player through the
actual trigger dispatcher; supplied QC time makes the real pinned teleport's
deadline numerically equal to the solver deadline. It checks immediate semantic
timer cancellation and a complete equal-ACK snapshot without another command.
Those callback/link/time contexts are fixture seams, not proof of natural
stock callback reachability. Geometry, QC, relocation recognition and snapshot
commitment are actual; no production test hook or transport is added.

The callback helper fixture additionally checks unchanged ownership,
deadline-only/flag-only takeover, same-value semantic relocation, preservation
of unrelated flags and that a flag cannot manufacture a private timer. The
pause fixture checks immediate semantic cancellation/preserved QC hold,
explicit no-hold release and untouched native-owned timer/flag/deadline fields.
The ownership fix preceded the coherent server permission/client contact-gate
change. The latter now permits experimental stock wet live replay under the
same existing authority; neither changes default selection or mod admission.

`stock_liquid_transit_fixture.c` reuses the admitted driver for actual BSP
surface entry/exit, delayed snapshots over three world frames, and multiple
received commands queued before one prepared longer world frame. It compares
actual live pending origin/velocity against completed movement, retains the
wire-derived bounds, then commits a complete snapshot and positive preview.
The prepared world-frame duration feeds the existing credit owner; per-command
QC/solver/completion remain actual. No collision/contents stubs are used.

```sh
make -C Quake -f ../tests/negotiation_native.make negotiation-native-fixture \
  USE_SDL3=1 -j4 NEGOTIATION_FIXTURE=/tmp/qsvr-stock-liquid-transit \
  NEGOTIATION_SOURCE=../tests/stock_liquid_transit_fixture.c \
  NEGOTIATION_EXTRA_EXCLUDE_OBJS='sv_phys.o cl_main.o' \
  NEGOTIATION_EXTRA_LDFLAGS='-Wl,--wrap=NET_SendUnreliableMessage -Wl,--wrap=R_TranslateNewPlayerSkin -Wl,--wrap=PR_ExecuteProgram'
/tmp/qsvr-stock-liquid-transit -selected -drown \
  -dedicated 3 -noudp -nosound -basedir "$SERVER_PROFILE" -userdir "$SERVER_PROFILE"
/tmp/qsvr-stock-liquid-transit -selected -fixturemap e1m2 -ledge-recovery \
  -dedicated 3 -noudp -nosound -basedir "$SERVER_PROFILE" -userdir "$SERVER_PROFILE"
```

The same liquid/map/VR/msec switches apply. Normal water/slime/lava, generated
VR and 10/25/100-ms matrices pass. Predicate-only prepared pusher, nonfinite
deadline and mismatched flag/timer inputs check actual snapshot permission
denial; they do not qualify actual pusher movement. Wet pause and arrival-gap
use real host pause/`svc_setpause`, fence/marker/command completion/full snapshot
and replay. Pause immediately invalidates the selected baseline/permission and
latches the observed generation without producing an old resume marker. The
driver also withholds paused/recovery snapshots, delivers unpause, then delivers
an old snapshot. Ordinary wet movement includes a newer pre-pause snapshot
from actual setpos relocation; a merely newer completed generation must not
masquerade as the resume fence. Replay stays denied until an actual newer
pending fence and marked command complete. The causal fix reuses standalone
private ACK metadata immediately before each selected recipient's reliable
pause service; it provides that actual pending fence even if the owner snapshot
is lost. UNKNOWN control authority cannot grant replay. Message-local source
context distinguishes a delayed old pause from a new pause at the current
generation. The inverse driver delivers pending and completed snapshots before
distinct delayed reliable pairs, then a fresh pause after completion. Rapid
toggles have no intermediate server tick; a third actually admitted selected
peer checks per-recipient epochs alongside public native pause bytes.
`-ledge-recovery` also checks active owned timers across both ordering directions
and ordinary recovery.
The focused sender, ACK parser and owner-snapshot fixtures pass ASan/UBSan.
Owner acceptance keeps the production rule that ACK must precede the next
producer cursor: produced command 10 uses cursor 11, while next-cursor 10/ACK
10 remains an explicit rejection case.

`-drown` prepares expired air/pain clocks, leaving actual pinned environmental
QC to apply health damage through death and native continuation. `-drown-only`
starts that case independently; compare separately initialized native and
selected runs by omitting/including `-selected`. The observed ten damage-event
frame/time/health/depth tuples agree. Do not claim arbitrary hazard/combat
prediction from these events. A one-command dry→wet→dry path remains covered
by the synthetic-hull shared-solver fixture, not by an actual stock BSP case.
Captured delivery and prepared starts/velocities/signon/resources/input retain
the same explicit component boundary.

For combined-fixture sanitizer coverage, build the ordinary Linux objects first,
use a fresh `NEGOTIATION_FIXTURE` output name, add
`CPPFLAGS='-fsanitize=address,undefined -fno-sanitize-recover=all -Wno-error=format-overflow'`
to the make command and append `-fsanitize=address,undefined` to
`NEGOTIATION_EXTRA_LDFLAGS`. Run with `ASAN_OPTIONS=detect_leaks=0` in this
environment. Physics/replay/server snapshot/client parser/demo sources in the
combined translation unit are instrumented; remaining objects are normal,
so this does not establish whole-engine sanitizer coverage. The warning
exception retains an inherited `CL_SetInfo` format-overflow diagnostic seen in
this instrumented compilation; ordinary Linux builds still use `-Werror`.

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

Private stock runs default to `QSVR_LOCAL_EXPECT_PREDICTION=1`; setting it
explicitly makes the expected owner clear. For a fresh server with
`+sv_private_pmove_walk 0`, or an excluded native/mod admission, set0. Public
and unchanged upstream runs expect0.

For an explicitly public-only server, start a fresh dedicated server with
`+sv_qsvr_private 0` and use `QSVR_LOCAL_EXPECT_PRIVATE=0` with a separate result
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
For example, with the fresh explicitly public-only server running:

```sh
QSVR_LOCAL_EXPECT_PRIVATE=0 \
QSVR_LOCAL_ASSERT_PUBLIC_MOVE_STATS_OFF=1 \
QSVR_LOCAL_RESULT="$CLIENT_PROFILE/public-move-stats-result.json" \
  timeout --signal=TERM 150s gdb -nx --batch \
  -x tests/local_private_legacy_peer_smoke.gdb --args "$QSVR_BINARY" \
  -novr -nosound -window -width 640 -height 480 -basedir "$CLIENT_PROFILE" \
  +vid_vsync 0 +host_maxfps 144 +connect 127.0.0.1:28790
```

For a selected private PMove server, set
`QSVR_LOCAL_ASSERT_ACTION_ACK=1` on the private client command. It records the
first attack command sequence and fails if authoritative shell consumption
appears before the completed move ACK reaches that sequence. This checks one
visible action/ACK ordering path; it does not prove every queued action, VR
weapon pose, or packet-loss case. Run a fresh selected server with
`+sv_private_pmove_walk 1` (the stock default), and keep any explicitly
disabled/native result separate.
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
checks. This assertion alone does not require prediction; use an explicitly
disabled native server for that case. Against a selected server, set
`QSVR_LOCAL_EXPECT_PREDICTION=1` as described above.
Add `QSVR_LOCAL_ASSERT_PMOVE_TYPE=1` for a selected WALK server to require the
received owner `pmovetype` to be WALK (3), with its grounded bit matching the
owner's existing `EFLAGS_ONGROUND`. The explicitly disabled run expects prediction
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

## Stock production-default activation components

The [preimplementation activation plan](../docs/predictive-stock-activation-2.0-plan.md)
records source review, software evidence and remaining parent scope.
`-defaultselection` in the mixed/liquid/pusher drivers asserts untouched
`sv_qsvr_private=1` and `sv_private_pmove_walk=1` before actual offers/spawn/begin.
It performs no activation-cvar override. `-selected` explicitly chooses1;
omitting both flags explicitly chooses0 for the native reference.

Build the negotiation driver with the common make target above and no extra
object exclusions; its untouched-default case checks transport only and requires
selection to remain absent until `begin`. Build the mixed driver with
`NEGOTIATION_SOURCE=../tests/mixed_native_fixture.c` and linker wrappers for
`NET_SendUnreliableMessage`/`R_TranslateNewPlayerSkin`; its ordinary stock run is:

```sh
/tmp/qsvr-mixed-default -defaultselection -earlypause -arrivalgap \
  -dedicated 3 -noudp -nosound -basedir "$SERVER_PROFILE" -userdir "$SERVER_PROFILE"
```

The existing actual-code driver checks private generated VR/public desktop
movement/fire, full snapshots/replay, native cheat transitions, recovery,
teleport and death/respawn. Use the same binary without those three options
for explicitly native private/public gameplay. Build the liquid transit and
pusher drivers as described in their sections and replace `-selected` with
`-defaultselection` to qualify wet/pause and planted-pusher default admission.
No real device input or connected cross-play is inferred.

The distinct initial-exclusion driver reuses the liquid bootstrap:

```sh
make -C Quake -f ../tests/negotiation_native.make negotiation-native-fixture \
  USE_SDL3=1 -j4 NEGOTIATION_FIXTURE=/tmp/qsvr-stock-default-admission \
  NEGOTIATION_SOURCE=../tests/stock_activation_native_fixture.c \
  NEGOTIATION_EXTRA_EXCLUDE_OBJS='sv_phys.o cl_main.o' \
  NEGOTIATION_EXTRA_LDFLAGS='-Wl,--wrap=NET_SendUnreliableMessage -Wl,--wrap=R_TranslateNewPlayerSkin -Wl,--wrap=PR_ExecuteProgram'
/tmp/qsvr-stock-default-admission -admissioncase wet \
  -dedicated 3 -noudp -nosound -basedir "$SERVER_PROFILE" -userdir "$SERVER_PROFILE"
```

Run one case per process: `wet`, `noclip`, `disabled`, `load`, `program`,
`slots` or `elevators`. The last sets actual elevator mode2 before a static-floor
begin, avoiding later selected platform rejection. Wet uses a real BSP location and actual relocation/water categorization;
noclip uses the actual engine command. Load/slot/identity exclusions are prepared
admission metadata, not actual save/load, local-SP transport or foreign-mod QC
proof. Each actual `begin` remains native, repeated `begin` cannot retroactively
select it, and actual commands/native QC movement/fire/full snapshots complete
without replay permission or injected selection. Captured delivery and prepared
signon/resources/input retain the same explicit component limits as the shared
drivers. Parent mod/load/mixed-gameplay requirements stay open.

The stock intermission driver adds actual game progression:

```sh
make -C Quake -f ../tests/negotiation_native.make negotiation-native-fixture \
  USE_SDL3=1 -j4 NEGOTIATION_FIXTURE=/tmp/qsvr-stock-intermission \
  NEGOTIATION_SOURCE=../tests/stock_intermission_native_fixture.c \
  NEGOTIATION_EXTRA_EXCLUDE_OBJS='sv_phys.o cl_main.o' \
  NEGOTIATION_EXTRA_LDFLAGS='-Wl,--wrap=NET_SendUnreliableMessage -Wl,--wrap=NET_SendToAll -Wl,--wrap=R_TranslateNewPlayerSkin -Wl,--wrap=PR_ExecuteProgram'
/tmp/qsvr-stock-intermission -defaultselection -dedicated 3 -noudp -nosound \
  -basedir "$SERVER_PROFILE" -userdir "$SERVER_PROFILE"
```

Prepared relocation reaches the real e1m1 exit volume. Actual trigger/Think QC
freezes living players, the production reliable fanout/parser delivers the
intermission, and frozen quiet/batched generated VR commands cannot integrate
the body or receive replay permission. PreThink/PostThink execute once with the
native world clock; ACK/queue completion remains correct. Actual IntermissionThink
accepts a button after its natural deadline, queues changelevel and executes
the host/world reload to e1m2; real refreshed serverinfo/spawn/begin selects anew.
Omitting `-defaultselection` explicitly compares native admission.

`-finale` uses the real end map/boss/train context and directly activates the
boss's installed death callback with a prepared player `other`; natural boss
combat and scene rendering are not claimed. `-freezephase pre|think|post` uses
prepared composition after actual player QC/installed due Think to invoke the
actual exit callback, covering existing continuation/publication boundaries.
Add `-phasebatch` or `-phasequiet`; completed callbacks must not run again,
unconsumed batch tails complete in the next frozen native frame, and a quiet
transition does not invent a command ACK. Unrecognized living NONE without the
actual intermission global remains a predicate-only rejection check.

Add `-delayedcontacts` for the physical-contact regression. Actual impulse1
selects the stock axe, and an encoded live contact sequence produces its QC
whiff/cooldown as a positive control. After actual freeze, the same valid samples
arrive before the client parses reliable intermission. Selected/native0 cases
must preserve cooldown/hostility/cue counts, clear contact continuity and retire
commands without replay; normal map progression still completes. Generated
physical contact poses are a component input seam, not a device-tracking test.

The combined component prepares separate host/client context at command-buffer
execution, discards unexecuted bootstrap quake.rc/map-start commands, and
captures the reliable reconnect broadcast at `NET_SendToAll` because its
synthetic socket has no live driver. This adds no delivery/ACK simulator or
transport service and proves no reconnect routing/connected signon. Linux and
focused partial-instrumentation sanitizer checks complement those explicit
prepared/captured boundaries; device/platform/performance remain deferred.

## Selected stock moving-brush adapter

The preimplementation plan and review disposition are in
[`predictive-stock-pushers-2.0-plan.md`](../docs/predictive-stock-pushers-2.0-plan.md).
This component fixture imports the liquid/mixed bootstrap, actual command
admission and codec, pinned stock QC/BSP physics, complete snapshot parser and
client replay. Prepare the disposable stock profile described above; its pak
symlink must remain read-only. Build from the repository root:

```sh
make -C Quake -f ../tests/negotiation_native.make negotiation-native-fixture \
  USE_SDL3=1 -j4 NEGOTIATION_FIXTURE=/tmp/qsvr-stock-pusher \
  NEGOTIATION_SOURCE=../tests/stock_pusher_native_fixture.c \
  NEGOTIATION_EXTRA_EXCLUDE_OBJS='sv_phys.o cl_main.o' \
  NEGOTIATION_EXTRA_LDFLAGS='-Wl,--wrap=NET_SendUnreliableMessage -Wl,--wrap=R_TranslateNewPlayerSkin -Wl,--wrap=PR_ExecuteProgram'
/tmp/qsvr-stock-pusher -selected -dedicated 3 -noudp -nosound \
  -basedir /tmp/qsvr-native-transitions/selected \
  -userdir /tmp/qsvr-native-transitions/selected
```

Run one case per process. Omitting `-selected` selects the independently
initialized native body reference. The basic case activates real e1m1 lift
`*7`, checks exactly one carry during quiet and batched frames, then jump-off
and actual replay return. `-vr` adds generated VR body input; `-commandmsec 10`
or `100` covers shorter/longer command durations (default25ms).

Additional selected cases:

- `-palms`: real reliable capability exchange and raw hand codec, planted
  hands plus body support, a deliberate subthreshold stroke, quiet/batched
  movement, local anchor height and OFF.
- `-palms-only` (also10/100ms): body stays on static world floor beside the
  lift. Stationary/moving quiet frames have no frame interaction mark or new
  completion, yet retained palm state correctly denies replay. Batched strokes
  and OFF/replay return are checked; passive palm-only carriage is not claimed.
- `-blocked` (also native) and `-palms-blocked`: prepared physical ceiling;
  actual stock `plat_crush`, damage, reversal and geometric rollback. The palm
  case additionally uses a prepared trigger/destination and actual stock
  `teleport_touch` to fence result publication.
- `-door` (also native): actual targeted e1m1 door`*17`, prepared invocation of
  installed `door_go_up/down`, native non-rider push and replay recovery. No
  mover velocity/Think fields are supplied; natural button progression is not
  claimed.
- `-palms-replace` and `-palms-retire`: actual registered QC `setmodel` builtin
  or `ED_Free` invalidates an already published palm binding.
- `-pending-replace` and `-pending-retire`: after real PlayerPostThink, a
  prepared composition hook invokes those same actual mutation owners while
  a fresh PMove contact is still unpublished. The publication guard must clear
  the candidate and raw baseline; the next quiet full snapshot restores replay.

Captured socket sends/skin upload and prepared client resources, registration,
input, starting positions, physical obstacles, callback activation/builtin
arguments and longer outer world frames are explicit component seams. They
do not qualify connected transport, device input, arbitrary QC or full-domain
default activation across arbitrary programs. Compatible stock owners now
select normally; these cases use explicit component policy unless
`-defaultselection` is supplied.

Focused ASan/UBSan builds use a fresh `NEGOTIATION_FIXTURE` path and add
`CPPFLAGS='-fsanitize=address,undefined -fno-sanitize-recover=all -Wno-error=format-overflow'`
plus `-fsanitize=address,undefined` to the extra linker flags; run with
`ASAN_OPTIONS=detect_leaks=0`. These instrument the fixture and included
selected movement/message/client owners, not every engine object. The warning
exception is for the existing `CL_SetInfo` diagnostic; the ordinary Linux
build still requires `-Werror`.

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

## Cooperative QuakeC native movement

`cooperative_qc_program.py` appends a prepared `SV_RunClientCommand` consumer and
standard-physics builtin declaration to licensed stock QC. Original QC code is
unchanged. All extracted/generated program data stays in a temporary asset
root; the repository includes neither the game program nor its assets. Prepare
an isolated root with `id1/pak0.pak` linked to the installed read-only asset, then
generate `cooperative/progs.dat` beneath that root:

```sh
python3 tests/cooperative_qc_program.py \
  --source-pack /path/to/licensed/id1/pak0.pak \
  --output /tmp/qsvr-cooperative-qc-calls1/cooperative/progs.dat --calls 1
make -C Quake -f ../tests/negotiation_native.make USE_SDL3=1 -j4 \
  NEGOTIATION_SOURCE=../tests/cooperative_qc_native_fixture.c \
  NEGOTIATION_FIXTURE=/tmp/qsvr-cooperative-qc-native-fixture \
  NEGOTIATION_EXTRA_EXCLUDE_OBJS='sv_phys.o' \
  NEGOTIATION_EXTRA_LDFLAGS='-Wl,--wrap=NET_SendUnreliableMessage -Wl,--wrap=R_TranslateNewPlayerSkin -Wl,--wrap=PR_ExecuteProgram -Wl,--wrap=PM_PlayerMove' \
  negotiation-native-fixture
timeout --signal=TERM 30s /tmp/qsvr-cooperative-qc-native-fixture \
  -dedicated 3 -noudp -nosound -game cooperative \
  -basedir /tmp/qsvr-cooperative-qc-calls1 \
  -userdir /tmp/qsvr-cooperative-qc-user-1
```

Require exit0 and `COOPERATIVE_QC_NATIVE_PASSED calls_per_hook=1`. Repeat with
`--calls 0` and `--calls 2` in distinct roots/userdirs, and with
`-defaultselection` for the one-call program. All four pass on Linux SDL3. The
normal loader maps the named standard builtin to347; the preimplementation
loader assertion failed. The fixture runs actual client offers/spawn/begin,
command producer/receiver, world QC/physics, both peer datagrams and the full
client parser. The prepared hook halves wish speed100→50. Resting starts use
real BSP floor traces: one25ms call gives speed12.5 and displacement0.3125;
two12.5ms calls give speed7.5 and displacement0.171875 under existing NQ friction.
Zero calls give no ordinary fallback body move. Private roomscale contributes
exactly0.5 units on its separate axis; public roomscale contributes0.

A prepared trigger dispatches actual loaded `SUB_Null`, then the wrapper nests
another loaded hook/builtin for a prepared nonmoving entity. This checks real
builtin reentrancy and exact enclosing PMove/movevars restoration; it does not
claim authored nested-mod gameplay. The trigger count equals the number of
standard calls, detecting duplicate native epilogue dispatch. The hook changes
the cursor-screen third word, and the native input scope must restore it exactly.
Direct backend probes check zero-time no-op and rejection of NaN duration,
excessive duration and infinite sequence without body/scratch mutation; they do
not exercise the VM fatal-error path.

Both peers remain visible. Default selection retains native snapshot
classification and authoritative correction, with predictive replay denied;
the accepted-command extension below executes its heads individually.
The fixture prepares signon/resources, resting starts, VR samples and nested
composition, and captures transport. It does not qualify arbitrary cooperative
mods, sockets, selected per-command execution, Gorilla locomotion, headset input,
Windows or ARM. The [plan](../docs/predictive-cooperative-qc-2.0-plan.md) keeps
those later stages in scope. Consolidated stock mixed-peer pause/arrival/native
return, q30 session/replay/publication, older AD shared-QC boots/composition and
native customphysics/Think checks pass with the same production changes.

## Cooperative QuakeC accepted commands

Use the same generator/native fixture build above with the one-call program:

```sh
timeout --signal=TERM 30s /tmp/qsvr-cooperative-qc-native-fixture \
  -defaultselection -commandchecks -dedicated 3 -noudp -nosound \
  -game cooperative -basedir /tmp/qsvr-cooperative-qc-calls1 \
  -userdir /tmp/qsvr-cooperative-command-user
```

Require exit0 and `COOPERATIVE_COMMANDS_PASSED` in addition to the preceding
marker. Linux SDL3 passes. This case uses actual sampled/encoded/received10ms
and15ms commands with distinct buttons/impulses and duplicated delivery. It
requires two PreThink/hook/PostThink lifecycles at their own input/host/QC time,
one world25ms scheduled Think, actual transformed displacement approximately
0.1625, speed7.5 and summed roomscale0.5. Full snapshots retire/ack only consumed
heads and continue denying arbitrary-hook client replay. Before the adapter,
the actual coalescing owner executed one lifecycle and failed this assertion.

The real producer then sends a50ms head. With only25ms server credit, maintenance
sees the last completed inputs at zero time, skips the hook (its QC counter
increments independently of duration), moves no body/roomscale and advances no
completion. The following world frame executes the retained head at50ms: actual
horizontal displacement1.25 and its separate roomscale1. An empty-queue control
likewise runs no hook/body/completion. Pre/Post still execute and scheduled Think
uses its existing once-world opportunity. A prepared PostThink schedules a due
function after that opportunity; the later head must wait rather than reopen it.

A prepared PreThink write installs actual loaded `SUB_Null` customphysics;
PostThink clears it again. The first head alone completes, roomscale is applied
once, credit is fenced and the second head remains queued despite restored
eligibility. On the following frame it executes normally. The generator includes
the real customphysics field, so this is the actual engine dispatcher/VM, not a
mock custom callback. Writes at callback boundaries, Think deadlines, zero bank,
fractional sampler carry and input axes are prepared diagnostic controls.

Signon/resources and transport remain captured as in the parent case. This does
not qualify arbitrary authored programs, sockets, Gorilla/instant-stop, full
wet/local/load compatibility, cooperative client prediction, hardware or other
platforms. Existing stock/q30/older-AD/customphysics regressions pass. See the
[plan/review disposition](../docs/predictive-cooperative-commands-2.0-plan.md).

Also run with `-defaultselection -invalidpost` (without `-commandchecks`) in a
separate userdir; require exit0 and `COOPERATIVE_INVALID_POST_PASSED`. A prepared
NaN deadline written after real quiet PostThink must trigger actual post-callback
validation/drop, leave the completed cursor unchanged and release scratch/entity
retention while the public peer remains active. The captured bootstrap allocates
an endpoint outside any network driver; this case assigns it the real loopback
close owner. The first attempt reached the correct drop but crashed during that
unprepared close. The corrected fixture passes; no production network fix or
connected socket qualification is implied.

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


## Cooperative standard physics VR identity

Build the cooperative fixture above with its `PM_PlayerMove` link wrapper,
then add `-vrinputchecks`. Run the one-call prepared program with and without
`-defaultselection`, and the two-call program with `-defaultselection`, each
with a separate userdir. Require exit0 and `COOPERATIVE_VR_INPUT_PASSED`.
The wrapper observes input then always calls the real solver; it does not
implement movement.

All three Linux SDL3 cases pass. The actual private producer/receipt, loaded QC,
standard builtin, BSP solver and full snapshot parser show same-start deep-water
jump displacement of 2.625 units for VR versus 1.48749 for private/public desktop
with one call. Two half-duration calls give 2.5625 versus 1.99377; those schedules
are intentionally not claimed equivalent. All finish with velocity100, showing
why final velocity alone cannot establish the VR swim rule. Parsed owner height
matches materialized body height within coordinate quantization.

Prepared `PMF_LADDER` starts show identical VR movement at pitch0 and pitch65,
with ascent from forward input. Desktop pitch affects movement; private/public
desktop controls match. This proves the existing ladder consumer, not authored
mod ladder detection or complete wet/local/load admission. Actual trigger links
invoke a prepared nested QC caller on a separate PM_NONE entity; its solver
input has no VR identity. The existing scratch/input restoration assertions
remain active. One/two-call roomscale-once and QC input transforms still pass.

An isolated temporary copy clearing only the borrowed identity fails the swim
assertion (exit134): all three same-start cases move1.48749 units. Production
files are unchanged by this negative control. The earlier unmodified backend
also failed the VR check. Zero-call and accepted10/15/50ms, duplicate, quiet and
retained-head regressions pass with the adapter. Linux production build passes.

QC, body starts, input clocks and transport/sign-on are prepared/captured; this
is not a connected multiplayer, physical headset or arbitrary authored-mod
qualification. Gorilla locomotion and instant stop are excluded from the goal
and this adapter copies neither their state nor tracked displacement.


## Initial nonstock movement states

The driver reuses actual offer/spawn/begin, world/QC/body, producer/receipt and
full datagram parsing. Use the prepared cooperative QC root from the cooperative
fixture above (one standard call), and an isolated older AD root with read-only
licensed id1/pak0 and older AD pak0/pak1 links. No program hash is modified to
manufacture foreign-program eligibility. The stock BSP e1m1 supplies real water
and collision in both roots.

```sh
make -C Quake -f ../tests/negotiation_native.make USE_SDL3=1 -j4 \
  NEGOTIATION_SOURCE=../tests/initial_state_native_fixture.c \
  NEGOTIATION_FIXTURE=/tmp/qsvr-initial-mod-state-fixture \
  NEGOTIATION_EXTRA_EXCLUDE_OBJS='sv_phys.o' \
  NEGOTIATION_EXTRA_LDFLAGS='-Wl,--wrap=NET_SendUnreliableMessage -Wl,--wrap=R_TranslateNewPlayerSkin -Wl,--wrap=PR_ExecuteProgram' \
  negotiation-native-fixture
timeout --signal=TERM 30s /tmp/qsvr-initial-mod-state-fixture \
  -initialstate wet -dedicated 3 -noudp -nosound -game cooperative \
  -basedir /tmp/qsvr-cooperative-qc-calls1 -userdir /tmp/qsvr-initial-wet-user
```

Require exit0 and `INITIAL_MOD_STATE_PASSED`. Repeat with separate userdirs for
`fly`, `noclip`, `custom-hull`, `customphysics`, `invalid-ground`, `stale-ground`,
`invalid-think`, `invalid-custom`, and `unsupported-type`. Run `wet` and
`invalid-gravity` against actually loaded older AD using `-game ad` and that
isolated basedir. All twelve pass on Linux SDL3.

The first empty-queue selected NATIVE snapshot precedes every world callback and
command receipt. It must parse all fresh private movevar receipts and usable
settings plus actual owner coordinates. ACK0 intentionally remains stale while
no command exists; accepted selected-owner metadata begins with real produced
commands. Tests never inject spawned/selected/replay bits. Prepared initial
owner state is validated without changing any native client or loaded QC actor
fields. Positive movement, a two-head accepted batch, completed/retired cursors
and public desktop movement are required. Repeated begin preserves live input.

Loaded SUB_Null customphysics replaces movement/command hook for seven actual
world frames; clearing it restores a positive cooperative body move without
re-begin. Older AD wet WALK moves through native physics, then actual setpos and
noclip0 return it to dry shared WALK/replay permission. Fresh horizontal movement
and cursor advancement follow relocation; one no-send world pass supplies
budget to the retained tail before asserting complete retirement. This proves
return behavior, not just a classification bit.

Negative cases refuse selection and repair only their prepared invalid values
after native begin before exercising native gameplay. In particular malformed
optional gravity is not normalized by production. Before the adapter initial
wet selection failed (exit134); before the builder preflight NaN gravity wrongly
remained eligible (exit134). The fixture diagnostics exposed two preparation
errors: setpos deliberately enters noclip, and first svc_setangle restores
spawn yaw. Actual noclip0 and intended synthetic sender axes correct those
without a production movement change.

Current stock/q30 defaults and cooperative command, VR swim/ladder and invalid
PostThink regressions pass. Licensed QC/assets are external; initial fields,
inputs/clocks, sign-on and transport are prepared/captured. This is not a live
multiplayer, authored initial spawn, all-mod, local/load or hardware result, nor
a claim of arbitrary cooperative client prediction. Gorilla/instant stop are
excluded from the goal and are not added by this adapter.

## OpenXR actual Vulkan creation and late attachment

`openxr_vulkan_creation_fixture.cpp` drives the production enable2 wrappers and
getProc forwarding. It checks runtime-merged API/names/queues/features, threaded
callbacks, cached creation dispatch and foreign-instance isolation, missing
entry points, exact returned-handle selection, failure publication, direct
renderer records and record lifetime across XR shutdown. Its `--real-vulkan`
mode simulates XR wrapping genuine headless Vulkan instance/device creation with
runtime-added supported extensions and actual core multiview. No HMD/frame/scene
or renderer performance claim follows from this device-creation check.

```sh
c++ -std=c++14 -DUSE_SDL3 -Wall -Wextra -Werror \
  -Wno-missing-field-initializers -pthread \
  tests/openxr_vulkan_creation_fixture.cpp \
  $(pkg-config --cflags --libs sdl3) -lvulkan \
  -o /tmp/qsvr-openxr-vulkan-creation
/tmp/qsvr-openxr-vulkan-creation
VK_ICD_FILENAMES=/usr/lib/chromium/vk_swiftshader_icd.json \
  /tmp/qsvr-openxr-vulkan-creation --real-vulkan
```

`openxr_late_binding_fixture.cpp` uses the production loader/discovery,
original-Vulkan qualification, session/action/swapchain/frame/teardown owners.
SDL loader, XR dispatch and Vulkan driver are simulated; handles are retained
from successful spied creation rather than an invented enabled-feature flag.
It covers no runtime at desktop setup, explicit discovery/attach/submitted
stereo frame, instance-loss retirement, rediscovery/new frame and fresh sample,
original-binding adoption of enable2-created handles, callback re-registration,
missing/exact/prefix/malformed/growing extension queries, wrong GPU/API, absent
legacy extension and handle mismatches. This does not run a loaded engine scene
or real GPU commands on borrowed XR images.

```sh
c++ -std=c++14 -DUSE_SDL3 -Wall -Wextra -Werror \
  -Wno-missing-field-initializers tests/openxr_late_binding_fixture.cpp \
  $(pkg-config --cflags --libs sdl3) -o /tmp/qsvr-openxr-late-binding
/tmp/qsvr-openxr-late-binding
```

The existing `openxr_enable_fixture.c` now also executes actual command/frame
transition/attach/retirement after ordinary desktop/instance loss. Discovery and
queue registration are spies; GPU resources are empty/prepared idle. It checks
explicit failure/retry latching and `-novr`; it proves scheduling/ownership, not
loaded-scene/asset continuity. The original session-recovery and Vulkan-boundary
fixtures remain regressions for enable2 and retained-session retries.

`openxr_layout_fixture.c` calls actual donor descriptor/pipeline-layout creation
with desktop stereo output off and multiview readiness off/on, on a real Vulkan
device with core1.1, enabled multiview, two views and at least six descriptor sets.
It does not draw shaders/scenes. Exit77 is a capability/driver skip, not a pass.

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -ffunction-sections -fdata-sections \
  tests/openxr_layout_fixture.c Quake/r_ssao.c -Wl,--gc-sections \
  $(pkg-config --cflags --libs sdl3) -lvulkan -lm -o /tmp/qsvr-openxr-layout
VK_ICD_FILENAMES=/usr/lib/chromium/vk_swiftshader_icd.json /tmp/qsvr-openxr-layout
```

2026-09-29 Linux checkpoint: SDL3 Make, creation/real headless creation, original
Vulkan boundary, session recovery, late-binding and command-owner checks pass.
The default hardware driver is inaccessible in the sandbox. SwiftShader supports
core multiview and six views but reports only four descriptor sets, so the real
layout-owner test **skips77**. Do not claim actual six-set layouts or loaded-scene
continuity from these checks. Live/gaze/performance and Windows/ARM tests remain
user-deferred; incompatible-device reconstruction and late foveation readiness
remain implementation work.


The late-binding parser's zero/oversized/endlessly growing and invalid-name
cases also pass ASan/UBSan (`-fsanitize=address,undefined
-fno-omit-frame-pointer`). Run the executable with
`ASAN_OPTIONS=detect_leaks=0` in this ptrace-managed sandbox: LeakSanitizer
cannot run here, so this result does not include leak checking.
