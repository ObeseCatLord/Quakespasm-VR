# Native large-map qualification

2026-10-01. Preliminary host client through production29129513, Steam Audio
disabled only in this host configuration. Native Vulkan on RTX4090, SDL3.4.16,
private validation1.4.357; tasks/native resource owners unchanged. This follows
group6 of the consolidated plan, not a performance benchmark.

The earlier mj4m1 receipt establishes native load, signon4, initial rendering,
screenshot and normal exit after the three generic two-edge face repairs.
The same current host client now passed shib1_drake (Tershibboleth), tavistock
(peril3.0), and ad_tears (AD): actual requested map, signon4, twelve completed
loaded server-world frames, native screenshot, normal inferior/command exit0,
no validation error/hazard or injected teardown. No heapsize argument. Native
protocol selected fte999. Main inspected all three native screenshots: world,
weapon and HUD are present; tavistock/ad_tears also retain the startup menu
overlay. This does not establish unobscured whole-map visual correctness.

Read-only selected paks and loose map/model/sound/QC/resource directories are
linked into three disposable writable profiles. User configs, saves and real
assets remain untouched. Profiles/logs/scripts live under the private qualification
content-matrix directory; aggregate logs/content-matrix-current.log records all
three successful outcomes. The gdb harness waits for an active server world and
excludes demo playback before counting loaded frames.

Two earlier harness attempts are retained as failures, not product regressions
or passing content evidence. The first accidentally counted startup demo frames.
The second timed out because long absolute test-profile arguments exceeded native
CMDLINE_LENGTH256 and omitted +map from the reconstructed cmdline cvar. The final
run uses -basedir . -userdir . with each isolated profile as its working directory,
keeping the native command within its established bound. No production command
parser change or new long-command feature was introduced.

Still outside these bounded passes: campaign traversal, saves/hubs, additional
format/malformed-input cases, stereo large-map behavior, detailed effects/input/
mod compatibility, full portable audio artifacts and measured performance.
