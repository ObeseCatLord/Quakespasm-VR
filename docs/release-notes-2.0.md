# Quakespasm VR 2.0

Quakespasm VR has moved from its Quakespasm/OpenVR base to **vkQuake**, adding
OpenXR VR while retaining Vulkan desktop play. vkQuake history is retained so
upstream updates can be merged. `main` is the maintained engine, `2.0` remains
the updater channel/branch, and `legacy1.0` preserves the previous engine.

## What changed

- OpenXR head and controller tracking, stereo Vulkan rendering, VR weapon
  handling, weapon wheel, HUD, menus, avatars and controller bindings.
- vkQuake graphics, multithreaded rendering/loading and large-map support,
  with configurable lighting, shadows, SSAO, MSAA, anisotropic filtering,
  particles, water and classic/enhanced models. Desktop remains playable.
- Modern predictive multiplayer movement with VR support; desktop and VR
  players can play together. Regular and classic co-op profiles are available.
- Optional runtime-supported foveation. Eye-tracked foveation requests eye
  tracking automatically; fixed foveation is explicit and never a fallback.
  Headsets without accessible eye tracking remain supported.
- A larger installed-mod browser, downloads, desktop mouse interaction and
  VR navigation prompts. Existing launcher/mod selection remains supported.
- Voice chat with microphone/open mic defaults in VR and optional push-to-talk.
- Automatic AD weapon calibration by verified effective model identity,
  including compatible reskins and AD-based jams; authored offsets are retained.

Recent fixes include restored launcher global-profile write-back, visible desktop
and VR crosshairs when enabled, corrected alternating brush-instance addressing
for Rooftop interactables, more robust spatial audio, rolled weapon/projectile
handling, and AD calibration inheritance across compatible jam weapons. Regular
co-op shares functional AD jump boots; classic co-op retains collector-only boots
by default. Graphics help explains active dynamic lights and entity contact AO.
AO retains all quality options and uses the appropriate desktop/VR path.

## Native uphill and Peril update

Linux and Linux ARM64 receive shared-solver uphill support fixes: climbing
no longer drops ground contact merely because slope movement has positive
vertical velocity, QC-authored jump takeoff remains airborne, and running
uphill landings regain support for subsequent jumps. Oversized collision bounds
use the existing private32-bit representation instead of overflowing compact
entity updates. Native desktop walking and ordinary compact bytes are retained.

Peril3.0 gains built-in calibration for all20 shipped weapon viewmodels and
controller-separated paired SMGs with native alternating fire, ammo and cadence.
The single rotary super nailgun remains single. Changed authored offsets retain
precedence; only unchanged copied stock triples yield to the Peril defaults.
Physical grip placement and animation appearance still need user testing.
Numbered PAK files with uppercase/mixed-case filenames are recognized on
Linux, restoring Zerstorer and other Windows-authored mod packages without
renaming assets. Windows is unchanged in this native update.

## Peril roster and desktop wheel update

Peril's weapon wheel follows its eight native selectable slots and twelve active
held-model identities. Shadow Axe, grapple, Widowmaker and Plasma Gun replace
their parent slot using the game's actual inventory modifiers, labels, models
and selection impulses. Offset-only calibration files do not invent extra
weapons or disable those replacements. All twenty packaged Peril viewmodels
retain their geometry-derived held and muzzle calibration and authored overrides.
Where the inherited desktop icon bank cannot depict the weapon correctly, the
wheel uses its native name rather than a misleading stock picture.

Desktop players can hold Q to open the screen-space wheel, select with the mouse
and release Q to equip. The wheel consumes mouse input without changing aim or
firing, and restores normal mouse capture after release or cancellation. The
Weapon Wheel entry remains available in Customize Controls with mod bindlists.
Q is a default binding; existing custom bindings and explicit unbinds retain
precedence. VR wheel placement and controller bindings are unchanged.

## Install or update

Use the launcher's **2.0** updater channel, or download the matching Windows
x64, Linux x86-64 or Linux ARM64 engine archive. Keep your launcher, licensed
Quake data, mods, saves and settings. No Quake or mod game data is included.
Extract the complete runtime together, including its libraries and notices;
do not mix executables and dependencies from different releases.

Start VR with `-openxr` (the existing launcher/wrapper `-vr` route is supported),
or desktop with `-novr`. VR requires a working Vulkan driver and installed
OpenXR runtime. The runtime controls headset render resolution. Steam Frame
native play uses Linux ARM64; PC streaming uses the appropriate PC build.
Eye-tracked foveation additionally requires accessible, configured gaze support
and the relevant runtime capabilities.

The launcher global profile uses `-postcfg <profile> -writepostcfg` for shared
settings. Plain `-postcfg` executes an override without writing it back.

## Source and acknowledgments

Matching engine and dependency sources are hosted separately and linked from
the GitHub release notes and the 2.0 updater release record.
Existing GPL/component notices and credits are retained. This engine builds on
vkQuake, Quakespasm, Quakespasm VR/OpenVR, QSS-M and Ironwail, plus the credited
runtime libraries. Development and review were assisted by OpenAI Codex agents;
changes are integrated and verified in this project.

Validation includes native Linux desktop and simulated OpenXR stereo rendering,
actual menu-driven AO/lighting pixel checks, native installed AD-family QC and
real two-client UDP jump-boot behavior, plus launcher profile shutdown/relaunch
and mod switching. Linux ARM64 receives native dedicated-server startup checks;
Windows x64 receives a native Release build and package inspection.

Physical headset/controller presentation, eye tracking, hardware-specific
foveation and performance measurements remain user validation. Windows runtime
and native Frame GPU behavior are not certified by cross-platform builds. The
[follow-up audit documents](https://github.com/ObeseCatLord/Quakespasm-VR/tree/v2.0.0/docs)
record narrower test limits and historical diagnostic failures. VR demo playback
is outside this project's supported scope; desktop retains vkQuake demo support.


The runtime archives include these instructions and component notices.
`SHA256SUMS` identifies the three engine downloads attached to the GitHub
release. Matching source-access archives and the exact engine source snapshot
are hosted separately; they are developer downloads and are not needed to play.
GitHub's automatic source ZIP/TAR contains only the repository at its release
tag, not the external dependency sources. Follow the matching-source links in
the release notes for updated native builds.
