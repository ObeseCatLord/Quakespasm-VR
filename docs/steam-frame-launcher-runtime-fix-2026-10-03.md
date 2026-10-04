# Steam Frame AD launch and launcher repair

## Plan and demonstrated incompatibilities

Retain vkQuake Vulkan, the existing native ARM launcher, its command builder,
and the existing game directory. Repair only Vulkan creation-dispatch capture
and the launcher-to-package boundary. Do not replace the renderer, disable VR,
change the system runtime/driver, or remove the mod picker.

Native Frame launch initially received a successful Vulkan device from the
OpenXR runtime but rejected it because no matching creation callback was
recorded. GDB showed SteamVR retrieving `vkCreateDevice` using an internal
instance rather than the application's pinned instance. Disabling the implicit
Valve FDM layer for one diagnostic process did not change that failure; the
final process retains the system's normal layers.

The next end-to-end check used the actual ARM command builder and the user's
settings. Its inherited library path placed SteamVR and old game libraries
before the package. That failed with missing `iplBinauralEffectGetTail` before
engine initialization. Shared `quake.cfg` and map arguments were not the cause
of this loader failure. The original user-reported “server closed” message was
not captured, so these are independently demonstrated launch blockers rather
than a proven attribution of that exact text.

## Implementation and senior disposition

`capture_proc` also captures foreign-instance device creation when its real
function pointer equals the pinned dispatch. It never repins dispatch from a
foreign lookup; genuinely different dispatch remains unchanged. Output-device
and selected-physical-device validation remain mandatory.

The executable `Packaging/Linux/quakespasm-openvr` adapter preserves the
existing launcher engine name, translates exact `-vr` to `-openxr`, resolves the
engine symlink, and prepends that package's sibling `lib` to inherited paths.
Argument arrays preserve spaces and token boundaries. The runtime selection
and other launcher settings remain under their existing owners.

Astra at explicitly verified xhigh reviewed the real patch read-only.

| Recommendation | Disposition |
| --- | --- |
| Same instance or identical foreign function dispatch | Adopted; no broad dispatch map or callback bypass. |
| Equality-only dispatch gate | Rejected; unnecessary tightening of existing target-instance behavior. |
| Explicitly executable adapter promotion | Adopted; source mode 0755 and installed mode 0755. |
| Reject successful but uncaptured output handle | Added fixture; rejected without publishing creation metadata. |
| Reject a device created for an unselected GPU | Added fixture; rejected without publishing creation metadata. |
| Package libraries before legacy launcher paths | Adopted; actual Frame launcher argument/environment smoke passes. |

The [OpenXR enable2 specification source](https://raw.githubusercontent.com/KhronosGroup/OpenXR-Docs/main/specification/sources/chapters/extensions/khr/khr_vulkan_enable2.adoc)
describes runtime aggregation and forwarded creation. [Valve's custom-engine
instructions](https://partner.steamgames.com/doc/steamhardware/steamframe/engines/custom)
remain the platform reference. No new foveation mode or quad views were added.

## Verification and deployment

- Production capture fixture passes, including shared foreign dispatch,
  distinct foreign dispatch isolation, threaded/cached callbacks, mismatched
  returned handle and unselected GPU negatives.
- Native ARM build on Foundry reused the verified d5cfff6c dependency graph and
  build objects; only `vr_openxr.cpp` and the engine link were rebuilt.
- Native launcher and update helper republished for linux-arm64 from the current
  launcher source without editing that source. All 20 launcher core checks pass.
- Direct real Frame AD start initialized stereo 1728x1728; exact launcher
  arguments/environment plus corrected adapter initialized stereo 2112x2112,
  executed shared `quake.cfg`, loaded AD start and logged player entry. Extents
  were selected by the runtime on each launch, not forced by this fix.
- A supplementary local full engine build regenerated against GCC 16 and failed
  on pre-existing `sv_phys.c` dangling-pointer warnings treated as errors. That
  unrelated code was not changed. The capture fixture compiles warning-clean
  locally and the native ARM shipping build succeeds with its existing GCC 13
  toolchain. No fresh PC deployment is claimed in this repair.

Installed native runtime:
`/home/steamos/.local/share/quakespasmvr/2.0/frame-launch-fix-20261003-193502/arm`.
Engine SHA-256:
`bc54ea780101f9e67b5bb54d6c0ffa4cd2ec8512c0a72cd1439b5d612ef26210`.

Executable-only backup (old wrapper, engine, GUI launcher, update helper):
`/home/steamos/Games/Quakespasm VR/executable-backups/20261003-193502`.
Game content and save files were not migrated or removed. The launcher is
selected for AD/start singleplayer with VR enabled; all other saved fields are
retained. Normal game execution may save its normal configs.

Local repair artifacts are under
`/home/obesecatlord/FastGames/qsvr-deploy-2.0-mtpuw_db`;
remote receipts and launch logs are under
`/home/steamos/.qsvr-deploy-2.0.BsQmrE`.
The installed-path promotion receipt is `frame-launch-fix-installed.json`.
Subsequent user launches use the installed engine, as confirmed by the running
process command and Steam's game output log; no automated final VR session was
started over the user's testing session.
The evidence establishes engine initialization and map entry on the physical
Frame; the user judges visible rendering, tracking, controls and play quality.
