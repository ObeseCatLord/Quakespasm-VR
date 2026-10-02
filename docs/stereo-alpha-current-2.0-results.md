# F05 native alpha scene: incomplete output qualification

2026-10-01. The test-only helper includes the actual cl_parse.c owner and uses
native late precache, Fitz static parsing, model loading, efrags and alpha lists.
Private initialization/opacity commands reuse normal command scheduling after
CPU submission retirement. No production rendering change was made. Native
assertion-enabled Meson compilation and client relinking both returned0.
[Before-code plan](stereo-alpha-output-final-2.0-plan.md).

Two generated v6 MDL inputs in the disposable stock profile are half-transparent
wet/dry quads. Native loaded identities, baseline/netstate alpha128 and exact
message consumption assertions passed. The settled diagnostic observed actual
under/over alpha counts1/1, model-center contents water(-3)/empty(-1), opposite
eye contents and wet mask1. Native MSAA4/SSAO1 remained enabled. Its normal
client/GDB exit was0. An earlier diagnostic paused before the client received
the queued teleport and is retained as a setup failure.

These counts do not prove correct visible composition. Inspected mirrors did
not show unmistakable colored fixture geometry; the cause remains unresolved.
The planned background/entity/water/combined layer captures were not produced.
Both subsequent attempts failed during native Vulkan device creation with
VK_ERROR_INITIALIZATION_FAILED; the fresh runtime also returned XRT_ERROR_VULKAN.
No composition, either-eye visibility or whole F05 acceptance is claimed.

The user then reported application crashes and broken MPV playback. The kernel
logged Xid51 BAD_TSG in monado-service and Xid154 selecting PF FLR recovery;
subsequent allocations reported NV_ERR_RESET_REQUIRED. Read-only nvidia-smi
confirmed recovery action Reset. This associates the fault with the test runtime;
it does not establish the underlying application/runtime/driver defect. No GPU
reset, driver reload or NVIDIA configuration change was issued by this work.
All owned native clients and isolated Monado processes were stopped and an
owned-process scan confirmed none remained. GPU qualification stays stopped.

The user's video opened with MPV's software wlshm output/hwdec=no and exited0
after the bounded muted playback check. No permanent MPV configuration change
was made. The user subsequently reported playback working, but NVIDIA still
reported Reset and recent kernel entries still required recovery. Playback is
not evidence that the GPU fault has cleared. A desktop GPU reset requires its
graphics clients to exit; a planned reboot is the recovery recommendation here.
No desktop application was terminated or machine rebooted by this work.
[NVIDIA recovery flags](https://docs.nvidia.com/deploy/a100-gpu-mem-error-mgmt/error-recovery-and-response-flags.html)
and [reset prerequisites](https://docs.nvidia.com/deploy/nvidia-smi/index.html)
document the recovery action and requirement that GPU clients exit first.

Private artifacts: stereo-alpha-current beneath the retained qualification root:
compile-commands.log/link-commands.log, assets.json, diagnostic/settled probes,
combined-left/right-settled.json and PNGs, layers.gdb, run-layers.log,
run-layers-fresh.log and private runtime logs. Keep failed attempts. Actual
layer output remains open until safe GPU qualification can resume; CPU/source
work can continue independently. No hardware/gaze/performance claim follows.
