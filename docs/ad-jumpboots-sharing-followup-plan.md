# AD jump-boots co-op sharing follow-up

2026-10-04; implementation complete, coordinated native fixture matrix passed;
private connected UDP qualification recorded below. Use the existing accepted QuakeC
pickup transaction and co-op profile policy; do not replay pickup QC for every
player or add game-directory checks.

## Verified behavior and gap

The installed AD `pak2.pak` source `my_progs/items.qc` assigns jump-boots
ownership to `moditems` bit 1048576 and initializes a cohesive configuration:
`jumpboots_finished`, `jumpboots_time`, `jumpboots_airmax`,
`jumpboots_height`, and `jumpboots_forward`. `finished == -1` and
`airmax == -1` mean unlimited, not smaller finite values. Native player QC
uses these fields for air jumping and clears ownership when the timer expires.
`jumpboots_airlvl`, sound and ground flags belong to each player's current
movement state and must not be copied from another player's in-flight state.

Before this change, `world.c` captured and shared added `moditems` bits, but did not
share the boot configuration. A recipient can therefore show ownership with
no working boot jump. Existing respawn snapshots contain boot fields, but
those are a separate lifecycle owner and do not establish a shared pickup.

## Incremental design

Extend the existing inventory snapshot/accepted-delta handling with one
recognized, type-checked boot tuple. Only a confirmed ownership/configuration
change through an accepted pickup may grant it. Preserve each recipient's
movement state; initialize fresh boot-use state as native acquisition does.
Use the existing co-op default/override gate: regular co-op sharing enabled,
classic co-op inherited sharing disabled. Never share on rejected/no-op touches,
on consumption, during singleplayer/deathmatch, or just because a trigger has
an artifact classname. Reuse existing team-progress snapshot for late joiners
and check native absolute expiry instead of creating another timer owner.
Preserve negative unlimited sentinels exactly; do not independently max five
fields and create a configuration no player acquired. Actual compatible QC
fields and inventory semantics select the adapter across AD-based mods.

Compare with replaying QC: replay would repeat SUB_UseTargets, randomizer,
item hiding/respawning, sound, and screen flash. Reject it; the native pickup
still runs once. Compare with folder-specific grants: names do not prove
inventory semantics. Reject that as well.

## End-of-implementation verification

Use actual installed AD-family program/item spawn/touch/player jump functions
through native server/QC owners with multiple initialized clients. Check
regular versus classic profiles, finite/permanent boots, tier/reacquisition,
independent used charges, rejected touches, expiry, joining and respawning.
A network two-client check should demonstrate replicated ownership and
functional jumps where available; component counters alone do not qualify
that behavior. No installed asset, mod configuration, or save edits.

## Implemented source and gain qualification

`Quake/world.c` adds 142 lines and replaces one line. The existing inventory
snapshot carries ownership plus five float configuration values, captured only
with a complete eight-float boot schema and compatible `moditems`. The existing
accepted-touch transaction, profile gate, level-progress cache, reset, restored
client merge and joining-client owner remain in use. There is no new protocol,
timer, directory whitelist or QC pickup replay.

Private packed-source reference: `/tmp/qsvr-ad-qc-reference/defscustom.qc`
lines 678 and 688–692 define the bit, typed fields and sentinels;
`items.qc:2938` initializes acquisition (including warning phase `time=1` and
fresh `airlvl=0`), and `items.qc:3490` spawns the actual artifact.
`client.qc:1388` consumes each player's own charges and supplies jump velocity.
`client_power.qc:167` reduces/removes powerups; its lines 492–507 advance the
warning phase and expire finite boots strictly when `finished < time`.

New live, typed ownership qualifies as a gain. With an already-owned bit,
sharing requires the trigger's declared boot bit, native post-acquisition
warning phase `time=1`, and a changed duration or movement configuration
(`airmax`, `height`, `forward`). A warning-phase-only change never qualifies.
Unrelated trigger warning/reset/expiry reductions cannot share. Native accepted
artifact replacements can lower a tier or replace permanent boots with finite
boots: recipients receive that exact acquired tuple, rather than independent
field maxima. This declaration/state check follows the verified QC contract;
it is not a general parser for every mod's pickup callbacks.

Fresh recipients initialize `airlvl=0`; existing owners keep their remaining
charges, and every recipient keeps its own ground/sound state. Late join uses
the cached absolute expiry without extending it. Native QC performs expiration;
an expired cached tuple cannot grant ownership. Restore keeps one coherent live
bundle in merge order rather than synthesizing one from multiple players.

Compatibility limitation: finding any boot field name reserves this bit from
the generic bit-only path, even for a partial/incompatible schema. A different
mod reusing one of these names and this bit could lose generic sharing and need
a separately verified adapter. The complete typed guard prevents a bare boot
bit from creating nonfunctional recipients. No broader mod list is introduced.

## Prepared native fixture and compile/link recipe

New `tests/ad_jumpboots_shared_native_fixture.c` loads unmodified installed QC,
checks its effective-program SHA-256, initializes four native client slots, and
executes real artifact spawn/think/touch, `PlayerJump`, `ClientPowerups` and
`ResetPowerJumpBoots`. Scheduled item-think time is staged because
`SV_RunThink` is private; the installed think supplies item setup and linking.
Pickup touches run through `SV_LinkEdict` and the production transaction once.
Movement inputs and geometry are staged; this is native functional QC evidence
when executed, not a connected network/Windows/VR session claim.

Cases cover regular/classic inherited policy and explicit overrides, finite and
permanent duration, infinite charges, tier and lower replacement tuples,
independent used charges, rejected and no-op pickups, warning-only reacquisition,
SP/DM and incompatible types, expiry boundary, real late spawn/begin, restored
coherent bundles and functional jump velocity. Unrelated-trigger negative cases
compose actual installed warning/reset QC after a real `SUB_Null` touch inside
the transaction. Only the permanent-to-finite rejection case stages an otherwise
unsupported field mutation: native reset QC never makes that conversion.
Peer configuration, local charges and cached joining state must remain intact.

After main completes its assertion-enabled native object build, from this repo:

```sh
python3 tests/run_ad_jumpboots_shared_native.py \
  --basedir /home/obesecatlord/Windows/Games/quakespasm_straight \
  --build-dir /path/to/main-native-debug-graph \
  --artifact-root /tmp/qsvr-ad-jumpboots-results-2026-10-04
```

The runner calls existing `run_csqc_entity_native.build_fixture`: it copies the
`sv_user.c` compiler recipe from `compile_commands.json`, compiles the new
fixture to `/tmp/qsvr-ad-jumpboots-shared-native.o`, and derives the link recipe
from `build.ninja`. It replaces exactly `main_sdl`, `sv_main`, `cl_demo` and
`cl_parse` objects (owners already included by the fixture), retaining all other
engine objects, including `world.c` and `sv_phys.c`. Link wrappers are
`Loop_Init`, `NET_CanSendMessage`, `NET_SendUnreliableMessage`,
`R_TranslateNewPlayerSkin`, and `PR_ExecuteProgram`. It neither invokes ninja
nor rebuilds a shared binary; stale `world.c` objects and non-Debug recipes fail.
Use a new artifact directory per run. `--games ad` narrows an initial run;
the default installed-QC matrix is ad, hwjam4, hwjam2, q30a1024,
quake_rooftop_jam_v2 and gibtropolis, each with private writable profiles and
read-only asset links. No QC is generated, patched or substituted.

The coordinated assertion-enabled graph was subsequently built by main at
`/home/obesecatlord/FastGames/qsvr-upstream-bonk-final-debug-20261004`.
Only the fixture object/executable was compiled/linked by this task; no shared
engine object or binary was rebuilt.

## Senior-review disposition and final native matrix

Main reported Astra's final production review approved with no P1/P2 findings.
Retain the existing production implementation; no new production architecture
or additional production changes are required. Adopt both requested fixture
repairs (also confirmed by main's spot-check):

1. Preserve distinct native-acquired finite/permanent QC payloads, reinstate the
   second payload immediately before its merge because the first merge updates
   every eligible client, assert distinct inputs, and verify the whole acquired
   tuple in both permanent-first and finite-first orders.
2. Carry nonzero, natively partly consumed recipient charges distinct from the
   picker across accepted upgrades and downgrades. Execute a subsequent actual
   native air jump to verify independent charge consumption and acquired boost
   velocity. A zero-to-zero assertion is insufficient to detect unwanted resets.

Both repairs were implemented and rerun in the full default installed-QC matrix
on the coordinated graph. Final retained artifacts:
`/tmp/qsvr-boots-native-final-20261004-c/profile-{0..5}/fixture.log`.

| Installed root | Effective program | Final result |
| --- | --- | --- |
| ad | pak2.pak:progs.dat | PASS, full native boots suite |
| hwjam4 | pak2.pak:progs.dat | PASS, full native boots suite |
| hwjam2 | loose progs.dat | NOT APPLICABLE: no boot spawn function or any of the eight boot fields |
| q30a1024 | loose progs.dat | PASS, full native boots suite |
| quake_rooftop_jam_v2 | pak0.pak:progs.dat | PASS, full native boots suite |
| gibtropolis | pak2.pak:progs.dat | PASS, full native boots suite |

All five applicable programs have unchanged SHA-256
`5e69fece92fb4323609c8e1209a39eecf4f70c3161ae17beb53063fe3e06c340`.
hwjam2 has unchanged SHA-256
`7f3cd7e5b06bc306de3d2ad2dd9182cc1c533f728075edeb5113dd04827ae605`;
absence is confirmed by actual loaded native program reflection and reported
separately, not relabeled a boot-support pass. The initial run caught a fixture
compile error accessing QC-only `attack_finished` through stock `entvars_t`;
reflection access fixed it. The next run exposed the unsupported hwjam2 program;
the fixture now explicitly distinguishes a wholly absent mechanic from a failed
compatible-schema test. Neither issue required a production edit.

Source/QC inspection, Python syntax and whitespace checks also passed. These
fixtures stage movement/geometry and capture transport; they are functional
installed-QC evidence but not connected-client or Windows/R2 evidence.

## Private real UDP dedicated + two-client proof

Final PASS for regular and classic profiles using the coordinated unmodified
`vkquake` binary, one actual dedicated process and two actual desktop clients
on loopback UDP. All reached native signon 4 with both named peers present;
real client/usercmd and snapshot UDP sequences advanced. The dedicated process
loaded unchanged installed AD QC with the five-variant SHA-256 above. No fixture
transport wrappers, generated QC, production commands or protocol changes were
used for this proof.

Private harness and retained final artifacts:
`/tmp/qsvr-boots-udp-private-20261004/run_udp.py`, `probe.py`, and
`regular-d/result.json`, `classic-d/result.json` plus per-process JSON samples
and logs. The private copy of installed `ad/pak1.pak:maps/start.bsp`, named
`boots_udp.bsp`, appends an entity lump containing one native
`item_artifact_jumpboots` (`cnt=-1`, `count=2`, `height=650`, `distance=0`).
Geometry and all other lump bytes remain unchanged. Profiles link installed
assets read-only; original maps, program, configs and saves are untouched.

Debugger probes observe server ownership/configuration/charges and client
snapshots. They queue ordinary `setpos`, `noclip` and `+jump`/`-jump` text in the
existing client command buffer. These commands then run through normal engine
command, usercmd, UDP, native player QC and snapshot owners. Gameplay fields are
not written by the network probes. Players are returned to walking and grounded
before jump pulses. Per-client command and UDP receipt waits avoid skipping
input on the slower CPU-rendered client. Client rendering is isolated to Xvfb
with SDL X11 and verified Vulkan CPU devices, both
`llvmpipe (LLVM 21.1.8, 256 bits)`; no GPU/VR input session is claimed.

| Profile (`shared_pickups=-1`) | Ownership | Peak server vertical velocity, collector / recipient | Peak received client velocity, collector / recipient |
| --- | --- | --- | --- |
| regular (`classic=0`) | Both acquire exact bit + all five config fields | 629.80 / 629.78 | 624.17 / 626.65 |
| classic (`classic=1`) | Collector only; recipient bit/config remain zero | 629.94 / 249.97 | 616.09 / 242.11 |

Configured native boost is 650; sampled values are lower because native gravity
has already advanced the frame. Classic recipient retains ordinary jumping.
The native fixture separately covers finite expiry, latejoin, exact sentinel
tuples, independent consumption, upgrades/downgrades and explicit profile
overrides; those cases are not claimed as independently exercised over UDP.

Setup attempts are retained transparently: the first real signon attempt
stalled on a GDB inferior call, resolved by staging normal command text in the
existing buffer. A subsequent functional pass used Wayland, so it was repeated
with explicit isolated X11. The first X11 attempt exposed skipped controller
commands on the slower client; acknowledgement waits fixed the harness, and
both final X11 profiles passed. No production fix was needed. All owned
dedicated/client/GDB/Xvfb children were stopped after testing. Windows/R2, VR
input, WAN and additional connected mod variants remain outside this proof.
