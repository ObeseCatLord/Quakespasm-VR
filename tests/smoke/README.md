# Runtime smoke tests

`run_smoke.sh` starts a built Linux runtime and checks that it actually plays,
using the freely redistributable shareware `pak0.pak`:

| Test | Checks |
|---|---|
| `dedicated` | A dedicated server loads e1m1, changes level to e1m2 and e1m3 from its console, and quits cleanly. |
| `render` | The desktop client (`-novr`) loads e1m1 and keeps rendering for 30 seconds. |
| `loopback` | A desktop client joins a co-op dedicated server over UDP and the server lists it. |
| `vr` | The client attaches an OpenXR stereo session to a simulated Monado headset (null compositor) and loads e1m1. |

Any `Host_Error`, `Sys_Error`, crash, AddressSanitizer report or unsuppressed
UBSan report fails the test. CI runs the suite twice on every push to `main`:
once on the release package, once on an engine built with
`QSVR_SANITIZE=address,undefined`. Rendering uses Mesa's software Vulkan
driver (lavapipe), so no GPU or headset is needed.

## Running locally

Run it in a container or on a private display; it never needs your desktop
session, audio device or VR runtime, and it unsets them for every engine it
starts.

```sh
tests/smoke/fetch_shareware.sh /tmp/qsvr-smoke/pak0.pak
tests/smoke/build_monado.sh /tmp/qsvr-smoke/monado   # optional, enables the vr test
tests/smoke/run_smoke.sh PACKAGE_DIR /tmp/qsvr-smoke/pak0.pak /tmp/qsvr-smoke/monado
```

`PACKAGE_DIR` contains `bin/vkquake`. `SMOKE_ONLY="dedicated loopback"` runs a
subset and `SMOKE_LOGS=DIR` keeps the engine and Monado logs.

Ubuntu 22.04 packages for the Monado build: `build-essential cmake ninja-build
git pkg-config libeigen3-dev libvulkan-dev glslang-tools libx11-xcb-dev
libxrandr-dev libxcb-randr0-dev libbsd-dev libudev-dev libusb-1.0-0-dev
libcjson-dev`. Runtime: `xvfb x11-utils mesa-vulkan-drivers libvulkan1`, plus
`libasan6 libubsan1` for sanitizer builds and `libarchive-tools` for the pak
fetch.

## Notes

- The shareware game ignores `+commands` on the command line (the engine only
  honours them for the registered game), so the tests pass start-up commands
  through `id1/autoexec.cfg`.
- `ubsan.supp` lists the UBSan reports that predate these tests. Add to it only
  for a report that is understood and harmless.
