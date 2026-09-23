# mj4m1 Linux Vulkan baseline

Status: 2026-09-23. This is an inherited crash gate and a protocol for collecting
performance data; no frame-time/FPS or RSS benchmark has been accepted yet.

## Observed result

The untouched vkQuake 1.37 archive and the 2.0 build both load `mj4m1`. Both
have observed quit crashes after client removal. In 2.0, explicit
`r_rtshadows 0` exited cleanly in two runs, `1` crashed in one run, and `2`
exited cleanly in two runs. Earlier 2.0 runs without an explicit shadow setting
crashed, but their saved configurations changed, so their effective setting is
unverified. Untouched upstream 1.37 crashed in two ordinary runs with explicit
`r_rtshadows 2`; two debugger runs with that setting exited cleanly. One clean
2.0 run at `r_rtshadows 2` also resized the TLAS, so resize alone is not a
sufficient trigger. These small samples do not establish mode-specific rates.

These observations do not establish a cause. The reference is the untouched
upstream 1.37 archive; the 2.0 source is at merge `51311ecc`, with the
`scr_speeds 3` timing change at `ff6a6be2`.

Vulkan validation was unavailable because `VK_LAYER_KHRONOS_validation` was
missing. The attempted `-validation 2` run on a release build did not enable
validation because that code is guarded by `_DEBUG`; a debug build could not
start with the missing layer. There is no validation-clean result for this gate.

## Repeatable protocol

Use identical canonical game data for both binaries, including the
`mjolnir/maps/mj4m1.bsp` and `.lit` assets. Keep each run's config/profile
separate and record binary revision, build type, GPU/driver, display or headset
refresh, resolution, and OpenXR runtime. Do not compare desktop and VR results
as one population.

Build 2.0 from the repository root with the Linux SDL3 release setup:

```sh
meson setup <build-dir> . -Duse_sdl3=enabled -Dwerror=false --buildtype=release
ninja -C <build-dir> vkquake
```

Build the upstream reference with the same compiler/build options where
available. For desktop, launch with a fixed window size and the same data root;
for 2.0 VR, use the same headset/runtime and add `-openxr`:

```sh
<build-dir>/vkquake -basedir <quake-data-root> -game mjolnir -window \
  -width <width> -height <height> +r_rtshadows 2 +scr_speeds 3 +map mj4m1
<build-dir>/vkquake -basedir <quake-data-root> -game mjolnir -openxr \
  +r_rtshadows 2 +scr_speeds 3 +map mj4m1
```

Run each engine/shadow-mode combination from a fresh process five times,
interleaving the runs. Use explicit `r_rtshadows 2` (medium) and
`r_rtshadows 0` (off); the menu values are 0=off, 1=low, 2=medium, 3=high.
Let the map reach fully playable signon, then issue `quit` with the same
timing in each run. Record load success, clean exit or crash, and whether the
crash occurred during client removal or later shutdown. The upstream comparison
is desktop; test 2.0 desktop and OpenXR VR as separate configurations.

For future metrics, time process launch to fully playable signon with a
stopwatch across the five runs. After signon, warm up for 30 seconds at a fixed
view or repeatable 60-second route, then capture `scr_speeds 3` once per second
for 60 seconds. Report median and p95 CPU, GPU, and wait milliseconds per run;
keep GPU samples separate because their fence result is delayed and is not
matched to the displayed CPU/wait frame. Also capture presented frame intervals
with the same telemetry method per mode: desktop Vulkan presents, and OpenXR
application/compositor frames plus missed or reprojected frames. Report median
and p95 frame interval and missed-refresh count separately for desktop and VR.
If RSS is measured, record peak process RSS for the same run. Record the
telemetry tool/version and route so later runs can repeat them.

`tests/README.md` documents `scr_speeds 3`: it shows CPU/GPU/wait timings, keeps
indirect rendering eligible under its normal conditions, and omits draw counts.
