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

Source and matching dependency-source archives accompany the runtime downloads.
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


The runtime archives include these instructions and component notices. Matching
source-access archives provide dependency sources/receipts; `product.tar` is the
exact engine source snapshot. `SHA256SUMS` identifies all six release downloads.
