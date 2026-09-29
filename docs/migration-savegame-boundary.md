# Multiplayer save migration boundary

Current local/restored movement evidence is recorded in the
[local/load checkpoint](predictive-local-load-2.0-plan.md). Actual v5 private
fastload/autofastload uses reconnect isolation; v7 restored first-player movement,
pending-save refusal, second named payload restoration and stale-ground native
fallback pass Linux software checks and final Astra Max source review. These
checks use prepared renderer resources and a synthetic second endpoint; they do
not close the wider save matrix described below. The earlier sections retain
their original planning/checkpoint chronology.

The current vkQuake loader in `Quake/host_cmd.c` owns legacy version 5 and KEX
version 6 saves, including KEX mod selection, extended precaches, fastload, and
the existing QuakeC entity/global parser. `Quake/savegame_dialect.h` already
classifies inherited multiplayer versions 6 and 7 before game or mod mutation,
but `Host_Loadgame_f` still rejects them. The inherited product writes version
7 with a max-client count, per-slot identity, colors, frags and spawn parms;
version 6 has a related layout without names. Its load path retains pending
clients for reconnect and restores their entity snapshots at spawn. The
version-6 number is shared with KEX, so dispatch by number alone is unsafe.

## Decision before implementation

| Option | Reuse and incompatibility | State/policy cost | Smallest proof |
| --- | --- | --- | --- |
| Extend the existing `host_cmd.c` save/load owner with dialect-specific header and client-slot adapters | Reuse vkQuake file lookup, KEX handling, entity/global parser, and preflight classifier. Adapt only inherited 6/7 header, per-client snapshots, and reconnect state. | One parser and one pending-client owner; bounded additional slot state in the existing server. | In a disposable co-op game, save with two clients including a dead player, reload, reconnect in reverse order, and retain identity/inventory. Legacy 5 and KEX 6 still load. |
| Import the inherited save/load functions wholesale | Replaces working KEX/mod/fastload handling despite no demonstrated incompatibility in the common entity and global format. | Parallel parsing or broad replacement, duplicated game-switch and error policy. | Same proof, plus retesting every donor save path. |

Use the first option. Treat any conflict in the common entity/global parser as
an observed incompatibility to resolve at that boundary, not a reason to
replace the entire loader. Keep save publication atomic and preserve the
inherited rule that automatic saves must not discard clients pending from a
load. Validate the full inherited fixed header and first QuakeC block before
disconnecting or switching the current game. Recheck dialect and source file
after any mod switch. Do not infer a multiplayer header from a bare `6`.

The implementation is incomplete until inherited 5/6/7 and donor KEX 6
fixtures exercise both parsing and behavioral round trips, including dead and
disconnected slots, name matching, late joins, hub progression, and autosave
rotation. A header classifier or emitted file alone does not establish those
behaviors. This is COOP-010 through COOP-012 in the feature map.

## Astra senior review disposition

Astra xhigh verified the current and pinned source before reviewing the
boundary. It found that accepting a version-7 header alone would be unsafe:
vkQuake's free-list rebuild includes empty reserved player edicts, whereas the
inherited allocator excludes them. This can let a later projectile take a
player's numbered slot. It also found that whole-edict snapshot copies would
overwrite vkQuake-owned identity, links and retention metadata.

| Recommendation | Disposition |
| --- | --- |
| Retain the donor loader, KEX handling, fastload and common entity/global parser. | **Adopt.** Add dialect branches at the existing owner; no parallel loader. |
| Exclude reserved world/player edicts from ordinary reuse and normalize empty saved slots before signon. | **Adopt.** Use `qcvm->reserved_edicts` at the allocator/free-list boundary; load restoration must leave absent slots inert and unlinked. |
| Keep pending snapshots in `server_t`, but copy raw donor edicts. | **Adapt.** Allocate and free the snapshot with server lifetime; preserve vkQuake edict metadata and restore QC payload/serialized attributes only. |
| Implement co-op inventory and progression inside the save loader. | **Reject.** Port the inherited typed dead-player inventory cache and shared-progression owner first, then call those owners from save/late-join code. |
| Validate the bounded inherited header before any world mutation. | **Adopt with a limited claim.** Check capacity, finite values, slot names and fixed fields; an opening QuakeC brace or lexical block check cannot prove later entity semantics. Recheck reopened KEX files after mod switching. |
| Preserve v5 single-player and inherited v7 co-op writing; add autosaves after manual round trip. | **Adopt.** Keep the inherited 16-spawn-parm fixed slot layout, explicitly handle donor-only extra parms, use checked temporary-file publication, and advance autosave rotation only after success. |

The corrected dependency order is: inventory/progression behavior, reserved
edict and snapshot lifecycle, dialect/sign-on adapter, manual co-op round trip,
then inherited autosave gates. Sign-on must distinguish a living saved client,
a dead saved client needing fresh entrance QC and typed inventory restoration,
and a genuinely new client needing default parms. A per-connection deferred
defaults flag survives another player's intervening restore. The smallest
behavior proof uses two named players plus an empty slot, reverse-order
reconnect, a dead player's exact inventory including zero ammo, a newcomer
accepted before the last restore, projectile allocation, spent shared keys,
and map-change snapshot invalidation. Device testing remains separate.

The first prerequisite is implemented at vkQuake's free-list insertion: edicts
below `qcvm->reserved_edicts` are never queued for ordinary allocation, and
the free-list diagnostic applies the same rule. Linux curl and no-curl links
pass. A disposable three-slot dedicated-server GDB probe freed player slot 2,
rebuilt the free list, and observed `reserved=4`, `slot_free=1`,
`queued_slot2=0`. The save loader's inert-slot normalization and the larger
round trip remain open.

## Manual co-op save/load checkpoint

The existing vkQuake loader now accepts inherited multiplayer v6/v7 after a
bounded header preflight, while retaining the donor v5 and KEX v6 paths. Player
QuakeC payload snapshots have server lifetime; reconnect restores only the
payload and serialized alpha into live reserved edicts. Manual co-op saves use
inherited v7 headers, checked temporary-file publication, and a trailer for
vkQuake's extended per-client spawn parameters. An active player must finish
signon before publication. Automatic save rotation is still pending.

An Astra code review found four P1 cases: late joiners retaining a free edict,
v6 reverse-order and v7 single-player rename matching, incorrect contiguous
reads of spawn parameters 17–64, and deferred newcomer defaults stranded by
`restart noload`. These were corrected in the existing spawn/load owner. The
review found no reason to replace the donor loader or add another state machine.

A disposable Linux dedicated server and Vulkan desktop client completed a
three-slot co-op connection, manual v7 save, load, named reconnect, and second
save. The second file retained the first player's header parameters. The first
attempt exposed a dedicated-server divide-by-zero in `Con_LinkPrintf`, which
assumed an initialized graphical console; link printing now falls back to
ordinary text there. Both Linux curl and no-curl builds pass, as do 51 fixed
header fixture cases. This proves a one-player round trip, not the full
two-player/dead-player/late-join matrix described above. A malformed inherited
save previously reached a rejection followed by an allocator abort during
graphical shutdown; its root cause and recovery path remain unqualified.
The same malformed file rejects with the expected `Host_Error` on a headless
dedicated server and exits without an allocator abort; this narrows the
unresolved abort to the graphical shutdown path or its interaction with the
rejection, without proving either cause.

After the reviewed edict fix, a second disposable run loaded that one-player
save, restored the named player, admitted a new desktop player into slot two,
and successfully saved both active players. This exercises the late-join live
edict condition but does not establish movement/collision or reverse-order
reconnect with two previously saved identities.

## Autosave checkpoint

The inherited map-start, secret, kill-bucket and serverflags triggers now call
the manual writer. Minimum interval, 1–20 slot rotation and retry timing use
the inherited policy. Pending restored players and incomplete signon leave the
scheduler state untouched; failed publication leaves the slot and progress
baseline unchanged. A disposable three-slot dedicated server with one desktop
client produced a v7 `coop_auto0.sav` at map start. Linux curl and no-curl
builds and the 51-case header fixture pass.

An Astra xhigh source review found no P0/P1 issue in the autosave adapter or
manual-writer extraction. The review retained the shared writer and existing
server-frame owner. It noted that synchronous save I/O, free-list checking and
save-list rebuilding may create frame stalls on large maps; `mj4m1` timing is
needed before choosing an optimization. Secret/kill/serverflags, publication
failure/retry and full multi-player restoration remain to be exercised in the
larger behavior matrix.
