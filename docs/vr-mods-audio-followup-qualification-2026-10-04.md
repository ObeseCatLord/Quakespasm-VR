# VR, mods, AD calibration and audio follow-up qualification

Implemented on `2.0`: matching main-menu Mods artwork and reachable catalogue,
profile-aware VR prompts, late-scene stereo crosshair rendering with corrected
pixel sizing, complete VR HUD/default Always Run, effective-model AD calibration
inheritance, and narrow Steam Audio input/output containment. The existing
renderer, downloader, model calibration schema and audio backend remain owners.

| Check | Result and limits |
| --- | --- |
| Native engine | Clang Debug with assertions and warnings as errors passes the integrated changes. |
| Main menu and downloader | Actual background X11 keyboard events navigate Main → Mods → Downloads. Quake-size/font/location artwork was visually inspected from native captures. The live existing catalogue returns 29 entries; the 24-row browser remains intact. Desktop and simulated OpenXR both navigate and quit normally. Installer/approval owner is unchanged. |
| Defaults | Actual embedded `default.cfg` execution gives a fresh VR profile `viewsize 100`, Always Run 1; desktop retains stock `viewsize 110`, Always Run 1. Saved config/autoexec ordering and explicit overrides remain native. No recurring reset or forced override. |
| Crosshair | The actual native foreground draw runs 154 times with OpenXR multiview and valid noncontroller aim. The simulator marks its HMD pose untracked, so the existing controller presentation guard correctly rejects that pose; this does not certify physical controller crosshair visibility. No synthetic tracked flag or bypass was added to production. |
| AD calibration | ASan/UBSan fixture runs the native effective PAK/loose filesystem. All fifteen built-in AD weapon identities pass for AD, Rooftop Jam, hwjam2 and q30, including hwjam2's older nailgun. Mixed assets, aliases, authored partial/custom triples and cached misses are covered. No frame-time file reads. Different asset versions fail closed to ordinary/authored calibration. |
| Binaural SDK boundary | Production mixer plus real SDK passes 81 consecutive injected failures: NaN/infinities on Apply and tail, disabled HRTF, dry blend, and finite SDK output overflowing after gain. Healthy second source and music survive exactly, final invalid-sample count remains unchanged and no callback allocations occur. Faults are returned-buffer injections, not proof of SDK-internal recovery. |
| Room SDK boundary | Production room owner plus real SDK passes 36 cases / 288 fault blocks in both modes with an actual worker-published IR: reflection/tails/decoder/voice, simultaneous bad branches and finite addition overflow. Fully rejected wet leaves dry unchanged. Latest maximum fault call 0.366 ms against 5.33 ms block duration on this PC; not a headset performance benchmark. |

Two Astra/xhigh passes retained the narrow architecture. The final pass added
source gain/accumulation overflow protection and removed a redundant outer room
transaction. Last-valid pose ownership stays in `SA_SetListener`; it rejects
nonfinite values/near-zero axes, not every mathematically malformed nonzero
frame. Main reviewed and applied the dispositions.

A real native rooftop callback probe observed zero final invalid samples after
the repaired SDK input boundary, versus 2,560 observed before. Audio clock,
source output and the off-thread room simulation continue. The exact reported
late PC all-audio cutoff remains unconfirmed; numerical prevention/containment
is verified separately from that causal claim. Native fault fixtures are
`tests/spatial_binaural_containment_fixture.c` and
`tests/spatial_room_containment_fixture.c`; owned test PCM/geometry only.

Prior upstream/anisotropy/Bonk/network/save/SSAO qualification remains recorded
in `update-qualification-2026-10-04.md`. The final release must be built from
one committed source archive for Linux, native ARM and native Windows, bind
matching runtime dependencies, then hash-verify R2 and both deployments. These
source acceptance results alone do not certify deployment or physical headsets.
No driver reset, global VR service change or user configuration edit occurred.

Final native OpenXR rooftop run: four samples through 20 seconds, 3,168 callback
blocks, moving clock through 614,400 frames, nonzero output, zero final/room/
binaural invalid counters and normal engine exit. Real keyboard movement was
queued through a private config and observed; it later stopped at a map wall.
The private runtime retained the existing PipeWire endpoint; no system audio
configuration was changed. This short check does not reproduce every late
cutoff trigger.
