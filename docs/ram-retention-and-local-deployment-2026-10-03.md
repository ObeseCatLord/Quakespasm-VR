# RAM retention follow-up and local 2.0 deployment

2026-10-03. Deploy the existing source/build `d5cfff6ce4cd5c36a75395aa02909f01093b4c51`; this task does not change engine implementation or start more benchmark runs.

## RAM assessment

The VR comparison measured whole-process RSS: approximately315→743MiB on AD/start and908→1248MiB on mj4m1. It did not separate heap, mapped upload buffers, allocator-retained pages or graphics-driver allocations. Those totals do not prove a leak or establish which component accounts for the difference.

A local Astra/xhigh read-only review finds plausible inherited vkQuake retained-capacity candidates:

1. **Staging buffers grow and do not shrink** (`Quake/gl_rmisc.c:777`). Uploading the installed Mjolnir world's427881 surface records at64bytes each requires26.116MiB contiguous staging. Two staging buffers retain at least52.232MiB, compared with the32MiB initial pair: roughly20.232MiB of allocation capacity might be reclaimed after loading, before padding/other uploads. This is not a measured RSS saving. Reuse the existing staging submission, fence and recreation lifecycle at a quiescent loading boundary.
2. **Dynamic vertex/index/uniform buffers retain their grown capacity** (`Quake/gl_rmisc.c:1270`). Their initial paired capacities total only3MiB. Old replaced allocations already enter garbage collection, so there is no evidence those old buffers are forgotten. Measure actual grown capacities before choosing a trim at map transitions.
3. **Empty device-local heap segments remain allocated** (`Quake/gl_heap.c:730`): texture segments64MiB, mesh segments16MiB. Retiring wholly empty segments could reduce GPU allocation pressure, but those sizes cannot be counted directly as process RAM savings.

Already handled: transient lightstyle planes, surface indices and workgroup inputs are freed after upload, and ordinary brush polygons are released after upload. Their existence alone does not explain persistent RAM use. The remaining CPU lightmap copies have runtime users; deleting them without establishing lifetime would be unsafe.

Main independently verified the staging growth/recreation path, dynamic retirement path, heap segment free behavior, and identical `Quake`/`Shaders` source between shipping `d5cfff6c` and current documentation-only commits. The review initially treated a concatenated brief revision label as unresolved; this source identity check resolves that limitation. Effective Astra/xhigh settings were verified from the reviewer's own turn metadata.

**Disposition:** adopt a narrow measurement-led plan to inspect existing `vkmemstats`, staging/dynamic capacities and process mapping/heap attribution on a large-map→small-map transition. Reuse pool retirement and add trimming only where the retained capacity and synchronization boundary are demonstrated. Do not reduce graphics quality, threading or rendering functionality merely to make RSS smaller. Full attribution and any code change remain follow-up work; they do not block deploying the existing build.

## Straight Linux deployment

Replaced only `quakespasm-openvr` (launcher) and `quakespasm-openvr.bin` (now a symlink to the installed packaged ELF). Existing launcher names still work; its argument adapter maps `-vr` to `-openxr` while preserving argument boundaries and `-novr` precedence.

Backup: `/home/obesecatlord/Windows/Games/quakespasm_straight/executable-backups/20261003-165400`. It contains only `quakespasm-openvr`, `quakespasm-openvr.bin` and `quakespasm-openvr.exe`. The Windows executable was backed up but remains unchanged. Mods, configs, saves, other binaries/libraries and launch scripts were not changed.

Runtime dependencies are isolated outside Straight at `/home/obesecatlord/.local/share/quakespasmvr/2.0/d5cfff6c/linux`. Existing exact packaged bin/lib entries were copied and hash/link checked; SDL3, Steam Audio and Vulkan loader resolution is verified from the installed Straight entry point. Native binary SHA256: `3ffd3418f4c4706a1b05a1894ab97de222a6b077de37dd3806e0213976bcf3b2`. Loader and launcher syntax/argument-preservation checks pass. No user game session was launched and no system runtime/driver settings changed.

To restore a backup, replace the `.bin` symlink with a regular copy of the backed-up `.bin`, rather than copying through the symlink into the installed runtime.

## Steam Frame

ARM runtime prepared from the exact same shipping source; native engine SHA256 `c1860ecf4be0e73a0e0c9b91228eeb0e2148efc37f1e5b3517e92019d90e44c3`. Existing verified package is reused, without rebuilding or copying game content.

The user supplied `ssh steamos@frame`. At the time of this record, that name does not resolve locally, and `frame.local` lookup also times out. Device deployment remains pending the current address/reachable SSH connection; no remote files have been changed.

Local deployment, loader, launcher, transfer and reviewer receipts: `/home/obesecatlord/FastGames/qsvr-deploy-2.0-mtpuw_db`.
