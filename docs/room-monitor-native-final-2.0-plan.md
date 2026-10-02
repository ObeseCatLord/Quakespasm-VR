# Native room / wet-only monitor qualification plan

2026-10-02, before fixture implementation. Frozen F08 after d18a430e. Main read
actual native room/SDK/public spatial/world adapter and independent voice monitor
owners plus earlier migration-spatial-audio-review and spatial-loop-monitor plan.
Preserve existing scene worker, effect, codec, capture, protocol and callback
owners; minimal optional native fixture only, no production rewrite/new service.

Official Valve guide recommends separate reflections simulation; C API reflection
effect requires separate input/output and exposes managed IR handles:
https://valvesoftware.github.io/steam-audio/doc/capi/guide.html
https://valvesoftware.github.io/steam-audio/doc/capi/reflections-effect.html
Main fetched tagged v4.8.1 SDK files to a private profile with URLs/SHA256.
api_simulator.cpp returns address of reflectionOutputs.overlapSaveFIR;
api_indirect_effect.cpp casts it to TripleBuffer<OverlapSaveFIR>;
actual simulation_manager.cpp partitions into writeBuffer/commits, convolution
updates read buffer. Current SAR worker owns simulator/GetOutputs serialized;
SDK handle is consumed only by hybrid effect, parametric effects do not consume
that IR. Scalar snapshot copied under existing lock; no extra IR copy/queue.
This is source verification, not a concurrency or foreign ABI qualification.

Use existing complete enabled graph, voice/audio fixture and capture-and-forward
renderer observer. S_Init only if absent under asserted dedicated/nosound. Actual
Spatial_Init creates DSP only; no physical output/GPU/OpenXR. Save globals/client
world/listener/native cvars and desktop profile inputs. All SFX/voice/music absent.
Prepare disconnected cls.state context; use native Voice_SetTransmitEnabled(false)
and Voice_SetSelfReverb(true), actual forced refresh/Voice_Frame. Dummy driver only.
Capture wanted/device ready with independent self permission and no session/TX;
cl outgoing count and voice_sending stay0. Never assign decoder/output/queue/clock.

Drain actual SDK self frames from dummy processing before injecting controlled
PCM. Existing Voice_EncodeCaptureFrame admits actual two20ms nonzero self frames;
with no room, consume them and require exactly silent output (wet-only/no dry).
Observe genuine SDK self_frames before/after. Third frame exceeds1920 bound,
keeps pending1920 and increments dropped count without network output. Native
Spatial_ResetSelf clears queued self/gain/remainder before next mode. Restore
local gain through public Spatial_SelfGain as a prepared DSP control only; real
Voice_Frame policy publication was separately exercised before controlled PCM.
Avoid subsequent dummy capture pumping while directed PCM/render runs.

Actual SpatialWorld_NewMap builds copied geometry from the already loaded native
sv.models[1] e1m1, prepared in cl.worldmodel; no alternate cube/mesh/BSP loader.
Save/prepare native listener vectors, origin from admitted player edict+22Z,
canonical forward+X/right-Y/up+Z. Public Spatial_Update publishes it/settings.
Fixture ray256/bounce2/reverb.5, real CPU scene default/SDK4.8.1. Poll actual
Spatial_RoomStats ready/runs with a bounded5s wall deadline and SDL_Delay10ms;
fail on stats.failed, require geometry/triangles/RT60 finite-positive. No simulated
result/run/clock. Worker remains actually concurrent with direct renderer calls.

Exercise room modes1parametric/2hybrid through cvars and real worker publications.
Feed real controlled capture-producer PCM into self ring, observe consumption,
finite nonzero output with isolated self path. Then reset self and verify silence
with room ready; repeat native permission-off, closed capture, no transmission
and rejected producer self-admission. Do not claim exact echoes/acoustic fidelity.
Privacy can leave real self PCM queued and a partial mixed block, then native
reset must erase queue/remainder/wet tails. Any partial-signal condition must
first be actually observed; do not invent positive output or blindly assume
one-block room latency. Bounded draws may use existing native ring/tail progress.

Replacement uses same actual SpatialWorld_NewMap owner and then clear. Linux
/proc/self/task comm snapshots observe room-acoustics named tasks before/after,
without touching any other process. Native join remains source-verified; require
observed retired task IDs absent after replacement, and original named-task set
restored after clear/shutdown. SDK child naming/count is not an exhaustive thread
or race/leak proof. No private room field/thread/IR access. Geometry pointer
ownership belongs to native world/room owners. Final attached-room shutdown must
also return cleanly with no observed owned room tasks. New ready room after clear
is required before final shutdown. Restore all prepared core/profile/cvar inputs
and forced native capture refresh for original connected fixture; preceding and
following native voice fatal/reset/Host_Shutdown must continue passing.

Luna/xhigh owns ONLY tests/room_monitor_native_fixture.h <=400C, defining guarded
Room_MonitorNativeChecks(void). No additional production/builder/header edits,
new linker wrappers or mocks. It is not alone/no reverting. Source-only/no builds,
tests/runtime/nestedagents/branch/commits; atomic>=16MB/fsync0644. Missing evidence,
needed architecture extension or cap overflow must be reported instead of widened.
Main owns include/optional call/guard/marker/private profile/docs/actual run and
local Astra review. Existing complete native graph reused, only fixture compiles.
One combined CPU-only run adds -room-monitor after prior eleven audio options,
fresh prefs/dummy/noudp/nosound/nosteamapi/licensed pak reads. Native actual object/
SDK/asset inputs stable through run; selected source hashes not exhaustive compile-
time/loaded-library attestation. No listening/physical recording/output/GPU/perf
or full F08/F10 claims. Reopen minimal adapter decision only on demonstrated bug.

Senior correction, before affected implementation: main verified ResetSelf does
not clear downstream underwater_accum; renderer emits its decaying history with
alpha<1. Keep existing callback-excluded reset boundary and clear those two floats,
without new state/policy/renderer. First strengthen fixture using public prepared
underwater alpha.5 and actual last-output sample/SA clock accounting: first-half
phase, queued self PCM, positive output/last sample; observe positive continuation
control, then independent native-reset and live public permission-off checks. No
intervening explicit reset before permission-off assertion. Source-only findings
are not yet reproduced. Main extends bounded helper beyond worker400-line cap for
this demonstrated correction; no ownership expansion/parallel implementation.
Actual named task membership remains narrow; sanitized RT60 and combined hybrid
wet output do not prove raw SDK estimate/convolution contribution. Restore known
prepared dry coefficient1 after this isolated DSP qualification. Preserve old
reviewed run before changes, reproduce residual failure, fix narrow native owner,
rebuild affected native graph and rerun consolidated fixture once. A production
change invalidates prior3204 shipping-source reconciliation for final artifacts;
Windows remains storage-blocked and ARM/Linux must be refreshed for final F10.

2026-10-02 resumed after disk space recovered. Measured post-revocation SDK float
sum2.584e-8/last3.045e-10 with active/stream/self0; origin of tiny residual remains
unknown. Actual SDL3 native output boundary in snd_sdl3.c clamps float then casts
to S16 with scale32767. Qualify silence at that concrete PCM boundary, not arbitrary
float epsilon/extra mixer muting. Record converted PCM energy/last sample per draw;
require live first half queued and last PCM magnitude>=8, positive converted
continuation control. Both explicit reset and live permission-off then require
converted PCM exactly zero. No claim of bit-exact float zero or actual device
callback execution. This strengthens the earlier tiny-float-positive live witness.
No-room wet-only check remains strict float-zero. Existing reset's two-float
clear remains necessary; source-verified stale filter state is not excused by
quantization. Reproduce the stronger PCM oracle against the previous native owner
in a separate private object/link, leaving current source/graph intact, then run
current fixed owner. No new production DSP gate or capture path.
