# Selected additions implementation plan

User selected CAND-UX-001, NET-003, PERF-001, UX-003, NET-005,
PERF-002, NET-004; priorities are the historical proposal priorities, not
permission to skip a selected item. Steam-install autodetection and mouse UI
must work; enlarge the mod browser substantially. Only branch 2.0 may change.
Windows verification is a final batch; no per-change builds. Performance
measurements, live headset/gaze testing and SSH deployment remain user followup.

| Feature | Existing owner and minimal integration | Completion proof |
| --- | --- | --- |
| Background save | Shared current serializer emits immutable bytes on main thread; bounded SDL I/O thread commits atomic file; main owns completion/autosave state | Existing v5/v7/co-op restore and delayed-write/error/lifecycle cases |
| Cancellable connect | Reuse existing NET_DatagramConnectStart/Frame/Cancel and CL autoreconnect lifetime for ordinary connects; retain local immediate connection | Unreachable target renders frames, Escape/disconnect cancels, valid connection and mod reconnect still work |
| Surface dithering | Port Ironwail surface/lightmap-coordinate noise into existing world shader, preserve existing palette/settings | Shader variants and anchored/stereo-coordinate checks; no speedup claim |
| Menu previews | Use current scene and menu veil; no camera or gameplay movement | Selected visual changes visible, settings persist, desktop/VR panels match |
| CSQC prediction API | Builtin345 history from existing move ring, client builtin347 through existing PMCL/pmove owner; one prediction owner | Representative QC reads historical/pending commands and reconciles without double prediction |
| Clustered lighting | Port Ironwail cluster maths at existing world/lightmap Vulkan boundary; one canonical cl_dlights snapshot; existing graphics remain default until user benchmarks | Lit moving/instanced brushes, both eyes, alpha/fullbright and shadows compatibility or explicit retained native path |
| ICE/WebRTC | Reuse QSS-M FTE-derived ice directory and net-driver boundary, native UDP unchanged; existing configurable broker protocol | Optional deps qualified; peer connection, cancellation, reliable/unreliable transport and lifecycle |
| Store discovery | Existing COM_FindStoreBaseDir and steam.c retained | Explicit basedir precedence, Steam library VDF paths and native original/rerelease choice |
| Mouse/mod browser | Existing menu pointer/slider/scrollbar owners; shared larger list/layout bounds | List/footer/search do not overlap, pointer hit testing and keyboard/VR controls stay aligned |

## Current evidence

Baseline implementation commit: 2b5928b72ed228cbd4b896be3197a787ce82c473.
References remain pinned QSS-M 03a498aabc411e2e739adc815c5536b161b9626e
and Ironwail 08d578136ff43d7d1ef38e636dfbfd3e844be7cd.
The old candidate absence document predates later 2.0 work. Datagram async
machinery already exists; only normal CL_TryEstablishConnection still calls
blocking NET_Connect. Do not copy another datagram state machine.
Current browser MAX_MENU_LINES is 14; improve substantially beyond that.
Both donors have thread-local qcvm, while target progs.h exports ordinary qcvm;
immutable byte snapshots avoid introducing background VM ownership.
QSS-M ships its ICE implementation locally and configurable FTE broker; no
new signaling protocol/service is justified by the selection. Native UDP
and ICE coexist at the driver table, not at co-op/gameplay state.
Official transport references: RFC8445 ICE and RFC8831 WebRTC data channels.


## Implementation and native batch result

All seven selected source slices are implemented. Astra xhigh reviewed the
save, renderer, graphics-menu and transport integration designs; disposition
tables are in their linked feature plans. Strict native debug compilation,
background/autosave, ordinary connect and ICE payload/lifecycle, loaded CSQC,
production cluster GPU compute and rendered menu checks pass. Separate actual
ordinary/ICE-UDP clients completed full private signon and predictive gameplay.
Ten live selected-lighting cases pass in desktop and simulated two-eye OpenXR;
the expanded menu also runs in both. Desktop menu captures were inspected.

Steam discovery and native mouse UI are retained existing Ironwail-derived
owners, not reimplemented. Explicit private `-basedir` wins in these native
runs. The mod browser now displays 24 rows instead of 14. New graphics controls
retain native defaults; optional cluster and dither remain off initially.

Physical gaze/foveation, measured performance and external NAT/browser tests
remain user qualification. Windows ICE needs an explicitly supplied TLS SDK;
native UDP networking remains available. Matching platform builds/publication
are performed after this source/native verification cohort.
