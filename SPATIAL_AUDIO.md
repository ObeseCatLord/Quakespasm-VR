# Steam Audio prototype

Experimental Linux backend with runtime environmental acoustics. Release-runtime
packaging and Windows SDK integration remain deferred. Normal builds retain legacy audio.

## Build and run

The flake builds Steam Audio 4.8.1 from pinned, hashed source using PFFFT,
libmysofa, FlatBuffers and zlib. No acoustic bake, Steam client, IPP, Embree or
GPU ray-tracing runtime is required. The SDK is a normal shared dependency in
the Nix closure; its Apache license is installed with it.

```sh
nix develop --command make -C Quake -f Makefile.linux USE_STEAMAUDIO=1 -j8
```

Outside Nix, install the SDK and provide `steamaudio.pc` (the local derivation
shows its contents). The Linux makefile defaults to `USE_STEAMAUDIO=0`, even
when pkg-config finds the SDK. Explicit `USE_STEAMAUDIO=1` requires the package
and SDL2. `USE_VOICE=0` independently
disables capture/network voice while retaining spatial SFX. Feature toggles
invalidate existing objects so switching configurations does not reuse stale
preprocessor choices.

Run from the development environment with your own game data:

```sh
nix develop --command ./Quake/quakespasm-openvr.bin -basedir /path/to/quake -vr
```

Use `-novr` for desktop testing; add `+map e1m1` or `+connect SERVER_ADDRESS`
as needed. Start your VR runtime first. Use a separate writable game directory
for isolated tests; never point writable config/save directories at another
installation through symlinks. Existing local microphone consent rules apply.
Run `scripts/run-spatial-audio.sh desktop +map e1m1`, or
`scripts/run-spatial-audio.sh vr -game qbj3 +map qbj3_draqu`, for isolated
configs and saves. `QSVR_DATA` selects game assets and `QSVR_PROFILE` selects the
writable profile (default `/tmp/qsvr-environment-$USER`). No microphone
preferences are copied from your working installation.

## Controls and listening checks

- `snd_hrtf 1` enables binaural processing; `snd_hrtf 0` uses panning through
  the same renderer and buffering, for a fair listening comparison.
- `voice_positional_only 0` retains the default distance-to-radio blend.
  `voice_positional_only 1` keeps valid sources positional at all distances
  (with distance attenuation); speakers without a usable position are silent.
- `snd_spatial_weapons 1` places recognized local weapon sounds at the VR
  muzzle. One-shots retain their emission position; loops follow the weapon.
- `snd_spatial_probe` places a looping fire sample 128 units ahead. Turn and
  lean around it. Optional arguments: `sound.wav` or `sound.wav x y z` for an
  absolute world position. Repeated probes remain independent sources.
- `stopsound` removes probes and current sounds.
- `snd_spatial_status` reports active sources, mono sample memory, DSP timing,
  latest-window output peak, clipping/nonfinite samples, voice backlog/drops,
  missed snapshot reads, and SDK allocation/free calls inside rendering.
  `snd_spatial_status reset` starts a new measurement window without stopping
  playback. Max pose age includes time spent loading maps unless reset afterward.
- Start with `-sndlegacy` to select the original backend. SDK-renderer creation
  failure also falls back to it. A missing/incompatible linked SDK can prevent
  the process from starting at all; `-sndlegacy` cannot fix loader errors or an
  unavailable audio device. The runtime HRTF toggle is not a backend switch.

Headset acceptance is still required: front/back and elevation, turning and
leaning around a stationary source, another player crouching, local weapon
placement, several simultaneous voices, radio transitions, and long-session
comfort/intelligibility. Verify ordinary menu sounds and music remain natural.
The metrics do not measure acoustic device latency or prove perceived location.

## Ownership and timing

`snd_steamaudio.c` owns playback cursors, per-source effect state, voice/music
rings and 48 kHz stereo rendering. It operates on 256-frame blocks and adapts
to arbitrary SDL callback sizes, retaining at most one block of remainder.
`snd_spatial.c` adapts Quake's channels, sample loading and source policies.

The main thread publishes control snapshots under a short spin lock. The audio
thread only tries that lock: if busy, it uses its previous coherent snapshot.
There is no callback wait on the game thread. Source generation IDs distinguish
slot replacement from gain/position changes, and atomic completion generations
feed channel retirement back to the game thread. This avoids a command FIFO
whose overflow could lose a stop. Start/stop operations replaced within a single
game frame coalesce as Quake channels do.

Quake's movable cache never crosses this boundary. Original WAV data is
resampled through SDL on the main thread into immutable mono floats, shared by
all instances of the same sample. These allocations persist across maps and
are freed on game-directory change or audio shutdown. The pool is bounded by
the engine's 1,024 known samples. Device-quiesced resets clear callback references
before reclamation. The old cache remains for legacy metadata/compatibility.

The SDL callback does no file loading, Opus decoding, entity access or allocation.
Steam Audio allocation callbacks instrument calls made inside the SDK as well.
Music decoding/resampling remains on the main thread. Natural track completion
drains the resampler and playback queue; explicit stop clears queued music.

VR publishes the head-center world pose before rendering the eye pair. Each
new DSP block uses the latest published pose. OpenVR pose acquisition remains on
the existing VR thread; this implementation does not poll OpenVR from audio or
predict independently. Network jitter buffering remains, but decoded voice stays
mono until playback. Mouth placement shares the renderer's visibility-independent
remote head-pose interpolation (including its 75 ms remote smoothing), adds a
small forward/down offset, and falls back to estimated mouth height. It is not
avatar-specific facial attachment or exact lip tracking.

Static sounds are not combined. Inaudible sources advance independently without
HRTF work. Music, leaf ambience and non-weapon local sounds use a dry path.
Filtering/resampling of low-rate SFX occurs before HRTF; the legacy mix-wide
low-pass is bypassed. Float contributions are summed and clipped once at output.
Existing SFX distance falloff and voice/radio gain rules are retained.

## Validation scope

The dedicated spatial fixture uses real Steam Audio, Opus and production jitter
code. It exercises handedness, late pose updates, arbitrary callback partitions,
translation invariance, loops/tails/replacement, packet-loss concealment and
recovery, radio fallback, reset boundaries, music wrap and concurrent publication.
It asserts zero nonfinite output and zero SDK allocation/free calls while rendering.
Benchmark builds omit sanitizers; the normal fixture uses ASan/UBSan.

The contributor's optimized synthetic benchmark on a Ryzen 7 9800X3D measured:

| Simultaneous sources | Mean per block | Worst observed |
|---|---:|---:|
| 8 | 0.049 ms | 0.066 ms |
| 32 | 0.206 ms | 0.230 ms |
| 128 | 0.831 ms | 1.234 ms |
| 512 | 3.363 ms | 3.847 ms |

The block deadline is 5.333 ms. These are bounded local measurements, not a
guarantee under VR/GPU/system load. The instrumented build was slower (512
sources exceeded the deadline). Scripted e1m1/e1m2 sessions exercised real WAV
loading, independent probes, HRTF switching, music, firing, stops and map changes
through PipeWire. Their measured DSP maximum was 0.091 ms with the small source
counts in that run. Both SDK allocation counters and nonfinite/clipping counters
remained zero. Headset listening and live multiplayer microphone quality remain
unverified. The bundled protocol-15 demo cannot be used on this upstream branch,
which requires protocol 999; the live-map smoke test avoids that unrelated limit.

The contributor reported a GCC/glibc `-Werror=array-bounds` failure in
`vrik_lowerbody_fixture.c`; the full fixture suite passed on the integration
review host. Compiler-specific failures still need to be reported separately
from focused audio results.

The prototype does not promise to HRTF-render all 1,024 potentially audible
channels within every callback deadline. Use the benchmark and active-source
counts on dense mods. It keeps sources separate and skips inaudible processing;
there is no hidden audible-source cap or sound merging.

## Later work

After headset validation: Windows x64 SDK/project/CI/runtime staging and testing;
retain the existing non-SDK Win32 build. Existing Windows builds compile the
no-op integration boundary and do not enable this renderer yet.

After tuning the static runtime acoustics below, consider moving brush models,
source-specific reflection paths and optional baked probes. A combined distributable needs the GPLv3-compatible license route
and appropriate component notices; this prototype does not constitute a complete
dependency-license audit.

## Direct environment controls

`snd_occlusion 1` enables smoothed static-world muffling on positional SFX and
voice. Moving doors/meshes are intentionally omitted. Voice uses a gentler gain
reduction and higher low-pass cutoff than SFX. Traces are bounded and staggered;
embedded sound origins can still produce imperfect results.

`voice_radio_filter 1` colors only centered radio with a 300–3400 Hz bandpass.
Set it to 0 for comparison. `voice_radio_compression` (0–1, default 0) and
`voice_radio_drive` (0–4, default 0) optionally compress/saturate that colored
branch. Existing radio volume and spatial-distance controls still apply.
These controls currently belong to the Steam Audio backend.

## Realtime room simulation

`snd_reverb 0..1` sets the room wet level (default 0.25). `snd_reverb_mode`
selects off (0), parametric (1), or hybrid early reflections plus a parametric
tail (2, default). `voice_reverb 0..1` sets the positional voice send (default
0.12); radio, music, menu sounds, and leaf ambience stay dry. Local movement
sounds and weapons excite the room. Loops use a lower send.

The client exports static BSP world faces at map load. Sky, liquid/teleporter,
and alpha-tested fence faces are omitted. A worker builds the CPU scene and
runs one listener-centered simulation with approximate hard-surface materials.
Moving brush models/doors are not included. Direct audio continues while the
scene builds or if setup fails. `snd_spatial_status` reports triangle/copy size,
scene-build time, simulation time/result age, and estimated decay times.

`snd_reverb_rays` (256–4096, default 2048) and `snd_reverb_bounces` (2–32,
default 16) are diagnostic quality controls. Updates run at most ten times per
second, with no queued backlog. Simulation uses a fixed 1.5-second response,
first-order Ambisonics, a 150 ms early-reflection window, and one CPU worker.
The convolution allocation retains the full response capacity; hybrid is not
a promise of proportional CPU savings from its shorter early window.

The Nix package includes a one-line Steam Audio 4.8.1 fix for an aligned load
inside its unaligned complex-accumulation path. This is necessary for reliable
multichannel convolution with this CPU/PFFFT build. Non-Nix SDK builds need an
equivalent fix if they use the affected path.

## Local microphone reflections

The Voice menu's **Local mic reverb** is a separate, saved opt-in for each desktop
and VR profile, off by default. Select a microphone on desktop first. It works
in single-player and with PTT released. PTT/VAD and Transmit microphone continue
to gate network packets only; the local feed is dry capture PCM sent exclusively
to the room's wet bus, without Opus, network jitter, or direct sidetone.
`voice_self_reverb_volume` (default 0.6, range 0–2) sets its send level before the
master `snd_reverb` wet level. `voice_self_reverb` reports the menu choice and is
read-only, like microphone transmission consent. Existing v1 preferences migrate
with local reverb disabled.

Capture closes outside active gameplay, during demos or after VR runtime loss.
Local reflection capture requires the Steam Audio backend. The existing 20 ms
capture chunks and game-frame dequeue add latency ahead of the reflections;
headset listening must determine whether jump/footstep reverb alone is more
convincing. There is no acoustic echo cancellation: use headphones, since speakers
can feed the wet return back into the microphone. Turning local reverb off clears
its queued PCM and the shared room tail (briefly clearing SFX reverb too).

## Environment validation (Linux, 2026-09-10)

Run the focused fixtures with:

```sh
nix develop --command bash -c 'make -C tests test-spatial .build/voice_settings_fixture .build/voice_primitives_fixture && ./tests/.build/voice_settings_fixture && ./tests/.build/voice_primitives_fixture'
```

ASan/UBSan fixtures cover radio frequency rejection and occlusion filtering,
synthetic-room hybrid/parametric tails, radio exclusion from room effects,
positional voice reverb, wet-only local mic output without network speakers,
local queue overflow/reset, scene teardown, and v1 preference migration with
self capture disabled. Production capture-purpose gates are exercised separately
from the unchanged PTT/VAD packet gate. These do not open a microphone.

Actual-engine smoke tests used an isolated profile, SDL offscreen video and dummy
audio, at 72 game FPS, with default 2048-ray/16-bounce hybrid acoustics. Each map
was allowed to finish scene construction before a roughly five-second DSP
measurement window. QBJ3 tests included jump input and a `player/plyrjmp8.wav`
probe; E1M1 used `player/land.wav`. Results on this Ryzen 7 9800X3D:

| Map | Exported triangles | Scene build | Simulation last / max | Audio block mean / max |
|---|---:|---:|---:|---:|
| E1M1 | 13,909 | 13.7 ms | 20.10 / 21.65 ms | 0.212 / 0.453 ms |
| QBJ3 DraQu | 558,409 | 705.4 ms | 25.61 / 26.69 ms | 0.267 / 0.508 ms |
| QBJ3 HCM | 736,587 | 896.0 ms | 39.79 / 41.65 ms | 0.380 / 0.707 ms |

All measured windows reported zero clipped/nonfinite samples and zero SDK
allocation/free calls during rendering. The same process changed between the
QBJ3 maps and shut down cleanly. The exported geometry copies were 12.0/16.8 MiB
for these QBJ3 maps; this excludes the SDK acceleration structure and DSP memory.
The simulation runs on a separate thread, sleeping 100 ms after each run, so its
time is not added to each audio block. Audio block deadline is 5.333 ms.

These are low-source-count desktop smoke tests, without headset/GPU contention,
multiple live speakers, real mic latency, or subjective listening. For headset
acceptance, compare `snd_reverb 0`/`0.25` and `snd_occlusion 0`/`1`; jump and walk
between a tight room, large hall and exterior. Compare nearby speech with radio,
then enable Local mic reverb and try it both with PTT released and transmission
disabled. Check intelligibility, tail loudness, self-monitor latency and comfort
before increasing wet gain. Music/menu sounds should remain dry. Moving doors
and per-source reflection paths remain intentionally outside this first pass.

Linux builds also passed with Steam Audio disabled/voice enabled and with Steam
Audio enabled/voice disabled; the final local binary has both enabled. An
actual-engine menu screenshot confirmed that all rows fit and a fresh isolated
SDL preference directory starts with local mic reverb off and no desktop input
device selected. The launcher isolates both `XDG_CONFIG_HOME` and `XDG_DATA_HOME`.

Subsequent VR headphone listening on QBJ3 confirmed that the basic environment
and self-reflection effects are compelling. The tester did not find the 20 ms
capture chunking objectionable for self reflections. A local dedicated-server
relay with a muted microphone sender and VR listener confirmed that the default
radio bandpass sounds clearly different from direct positional voice. These are
subjective single-user results, not a broad microphone or performance survey.
Additional radio hiss, squelch sounds and stronger distortion/compression are
deferred; the default bandpass is the accepted scope for this pass.
