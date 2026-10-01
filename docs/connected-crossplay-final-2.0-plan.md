# Connected desktop/VR software qualification

2026-10-01. Existing final qualification network slice; no new product feature.
Current prepared/captured fixtures do not prove real connected crossplay. Reuse
local_private_legacy_peer_smoke.gdb and pinned_vr_gameplay_smoke.gdb against
the current native dedicated server and two simultaneously connected clients.
The latter's name describes its historical peer, not a required old server.

Implement only a small Python orchestration runner: caller supplies debug client
and optional dedicated binary, assets, output and private runtime environment.
Create separate temporary server/desktop/VR profiles, link only licensed pak0
and an explicitly supplied weapon preset if needed, never installed configs.
Bind server to127.0.0.1 on an available explicit/allocated port, coop stocke1m1,
private profile and existing movement selection. Run both existing probes
concurrently: desktop requires two named peers, both require actual signon,
movement/attack/ACK and selected prediction according to their existing checks.
No usercmd/body/weapon/ammo injections beyond existing documented controller/key
inputs. Preserve full logs and structured results; pass only on both positive
result statuses/markers and successful GDB processes. Server/client shutdown
must target only subprocesses created by this runner, with bounded waits and
finally cleanup. Do not start/stop global runtimes or change external services.

Bound BEFORE220 new runner lines plus40 README lines; tests only. No edits to
existing probes or production until a concrete stale API/assertion is diagnosed
and planned separately. Syntax check now, actual run by main using existing
private simulated-Monado environment. Any unsupported probe behavior reports
exact failure instead of disabling a check. No hardware/performance/audio claim.
Public profile, reconnect/loss/metadata/voice cases remain separate existing
network-owner obligations; this runner closes only simultaneous private
desktop/VR software gameplay at real transport/render/input boundaries.

## Render/send cadence correction

The repaired actual connected trial reaches both named peers at signon4,
selected replay permission,117successful replay calls and25→21shells. It fails
the between-send presentation assertion with zero stable sent/ACK pairs. The
original README recipe explicitly disables vsync and raises host_maxfps to144;
the new runner omitted those diagnostic startup controls. BEFORE8 changed
runner lines: supply existing +vid_vsync0/+host_maxfps144 to both clients,
retaining native command/send cadence and every probe assertion. No prediction
field or packet timing injection. Rerun the same simultaneous case with fresh
output; observed replay calls alone remain insufficient acceptance.

The separated-render-cap attempt still reports no unchanged ACK/sent/authority
frames, while actual VR admission and90successful replay returns are observed.
Heavy GDB controller injection can make every rendered frame reach the default
network interval. BEFORE8 further runner lines: use existing native
host_phys_max_ticrate10 in this diagnostic profile so it can observe render
frames between native command sends. The native option, command sampling,
transport/server/ACK and all replay/output assertions remain unchanged. Record
the10Hz diagnostic limit explicitly; this is not default-cadence or performance
proof. A failure after actual stable frames is a different finding requiring
diagnosis, not another assertion relaxation.
