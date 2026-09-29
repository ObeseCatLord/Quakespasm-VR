# Local and restored private movement

Status: planned at `c3c13ac4`; local Astra Max review incorporated below.
This is a bounded continuation of the complete migration goal, not completion
of the inventory. [Current scope exclusions](migration-scope-decisions.md) apply.

## Verified behavior and decision

A local single-player VR/private session should use the same negotiated command,
completion and snapshot owners as a remote session. A valid restored player must
remain playable while other saved players are still pending. Public desktop
sessions retain vkQuake's existing single-player behavior. Saving must not
serialize transport queues or resurrect commands from the old connection.

Verified current source:

- `sv_main.c:SV_PrivateWalkTrialAdmissionFailure` rejects all single-slot servers
  and all `sv.loadgame` sessions, before existing owner validation. It does not
  actually distinguish loopback from remote sockets. Multi-slot local listen
  sessions already satisfy its connection condition.
- `cl_input.c:CL_SendPrivateMove` synthesizes completion and invalidates the owner
  snapshot for every active single-slot server. A pinned private session needs
  the existing received ACK/snapshot contract instead. Its normal startup skips
  and resume markers already exist; use them rather than invent a local cursor.
- `net_loop.c` owns paired local sockets; `SV_CheckForNewClients`, the extension
  offer consumer and `Host_Begin_f` are shared with remote sessions. No direct
  client-to-server movement/state copy is required.
- `SV_ConnectClient` clears client storage and calls the current queue/contact
  reset owners. `Host_Loadgame_f` parses the existing save dialects, and
  `Host_Spawn_f` restores QC payload before actual begin. Inherited saves keep
  `sv.loadgame` true while saved identities remain pending; v5/KEX also retain
  their established loaded-edict handling. That flag is not a movement owner.
- The existing begin/frame classifier and movevar builder validate actual
  nonstock owners. Initial stock dry/hull, elevator, profile, custom-stat and
  q30-policy contracts remain unchanged in this slice.

Use a minimal adapter: require a connection rather than multiple slots, remove
the blanket loaded-game rejection, and exempt pinned private commands from the
synthetic local ACK. Reuse all current queue, receipt, physics, snapshot,
pause/resume and save/sign-on owners. Reviewed production scope also includes
`host_cmd.c`: pinned-private fastload must use reconnecting load. Reuse begin
validation for stock admission to reject stale saved ground references before
selection. A separate local scheduler, save-movement
state machine or transport shortcut duplicates working owners and is rejected.
Reopen if those are needed or another persistent mode appears necessary.

Unknown: current real loopback single-slot command flow and restored movement
through these guards. Prepare client renderer/resource state in the native
fixture; do not claim complete graphical signon or live XR testing from it.

## Implementation and acceptance

1. Commit this verified plan; local Astra verifies before critique. Main
   prepares a fixture independently; production waits for disposition.
2. Apply the reviewed boundary changes. Missing connections/bots, disabled private
   movement and public offers retain native authority. No new mod cases.
3. Reuse native engine bootstrap, offer generation, baseline/resource setup
   and full message parser. Add a local fixture using actual paired loopback
   sockets and `NET_SendMessage`/`NET_SendUnreliableMessage`/`SV_RunClients`.
   Hold early resource-heavy serverinfo for existing header inspection; prepare
   client resources explicitly. Never forge selected/spawned bits or ACKs.
4. Single-slot stock private VR: actual offer/spawn/begin, real command receipt,
   movement/fire, completion after physics, parsed matching owner, usable stats,
   and replay. Verify no synthetic completion at send time. Exercise actual
   pause/resume and menu suspension; shared existing epoch/marker recovery.
   Repeat disabled/private-native and public desktop conditions.
5. Actual v5 save/load/reconnect must preserve QC player position/inventory and
   use fresh producer/server queues without replaying an unsent old command.
   Actual inherited v7 with two saved identities must admit and move the first
   restored player while the second remains pending. Restore the second through
   the existing named spawn owner; inspect state rather than clearing loadgame
   as a test shortcut. Use nonstock cooperative QC for this case if available.
   Cover explicit private `fastload`, private `load` with `autofastload`, and
   native fallback for a restored player standing on a pending saved identity.
6. Linux production build, focused matrix and existing mixed/cooperative
   regressions after implementation, then final local Astra source review.
   User hardware/performance tests and Windows/ARM builds remain outside this
   checkpoint. Broader save dialect/dead/late-join and mod replay goals remain.

## Senior review brief and environment

Solo maintainer; smallest shared-owner change. Verify the claims against the
real code, prioritize hidden local/save/queue interactions, and challenge the
necessity of extra architecture. Max700words, source/line evidence, disposition
recommendations and missing acceptance conditions. Read-only; no nested agents.
Do not re-review rendering, excluded locomotion or generic arbitrary-QC replay.

| Fact | Environment/evidence |
| --- | --- |
| Write scope | This `2.0` worktree only. Main owns production integration. |
| User edit | `docs/migration-2.0.md` is dirty and untouched. |
| Platform | Linux SDL3 Make build; headless native QC/BSP fixtures work. |
| Assets | Straight assets and prepared cooperative QC roots external/read-only. |
| Existing fixture seams | Dedicated bootstrap permits Loop_Init for testing; current mixed wrappers capture unreliable sends and hold reliable sends. Local fixture must forward actual loopback transport instead. |
| Coding model | Requested Luna is unavailable; main performs this bounded implementation. Astra is review only. |
| Lifecycle distinction | Loaded QC payload belongs to host_cmd; command queues belong to a fresh connection, never a save header. |

## Astra Max disposition

Review by Gibbs (`gpt-6-astra`, effective `max` verified), read-only. Main
spot-checked fastload's disconnect bypass, pending-player edict release, the
begin/frame validator, and private sender dispatch against current source.

| Recommendation | Disposition |
| --- | --- |
| Fastload can retain pre-load commands | Adopted: pinned-private single-slot load uses the existing reconnect path, including explicit fastload and autofastload. Public fastload retains its owner. |
| Stock admission can select a stale ground reference | Adopted: reuse observational begin validation after existing stock restrictions. Native fallback precedes selection. |
| Synthetic ACK belongs to the private sender | Adopted: remove it for every pinned session; preserve standalone startup skips and received ACK/snapshot/QC command frame. |
| Remove blanket single-slot/loadgame veto | Adopted: require a live connection and qualify each owner. Pending saved identities remain managed by the existing loader. |
| Acceptance must include actual host scheduling | Adopted: loopback receipt and parsing, menu/pause through Host_ServerFrame, reconnect isolation and pending identity movement. Prepared renderer state does not establish live graphical/XR behavior. |

The review added two concrete save/load safeguards. No new production state
machine, transport, save dialect, or mod policy is introduced.
