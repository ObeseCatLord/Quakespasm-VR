# q30 jump-boots pickup, native gameplay and replay return

Status: planned before implementation. Normal q30 session activation is committed
at `624aa154`; this closes one real-QC ability lifecycle in the broader
[AD-family plan](predictive-mod-admission-2.0-plan.md), not all ability/trigger or
mod compatibility. Linux software checks now; connected/headset/eye, performance
measurement and native Windows/ARM qualification remain deferred by the user.

## Behavior, evidence and reuse

A normally admitted private VR or desktop peer must remain playable when q30's
actual artifact pickup grants jump boots. The existing native frame must preserve
QC-authored charges, jump velocity and expiry. Snapshots must withhold replay
while the ability is owned and restore qualified ordinary replay after actual
QC expiry. A public desktop peer remains native and visible in the same world.
One weapon/muzzle calibration remains shared.

Verified current code: sv_phys.c's Q30State detects boots eligibility via the
pinned actual moditems bit1048576 before ability PreThink runs. The fresh native
frame and typed current-state predicate already own input, QC, completion and
snapshot authority. tests/stock_liquid_native_fixture.c imports those actual
owners plus existing mixed admission/full-send/parser/replay helpers. Its
StartLiquidPeers uses actual offers/spawn/begin and generated startup commands;
its -selected switch instead provides the existing native/default comparison
configuration. Existing q30_movement_native_fixture.c boots cases stage fields
and cannot prove pickup or full normal-session ability handling.

Verified installed program: SHA256
`5e69fece92fb4323609c8e1209a39eecf4f70c3161ae17beb53063fe3e06c340`,2347206 bytes.
The actual function table has item_artifact_jumpboots at1298/statement66273,
artifact_setup1287/65786 and artifact_touch1286/65485. Source-like decompilation
under /tmp/q30-source-check shows spawn precaching/setup, contact awarding
moditems and boots charge/height/deadline fields, then authored expiry clearing
the ability. Decompiled constant aliases are unreliable; resolve by actual VM
fields/functions and verify executed results. No source-like QC replaces the
installed bytecode. Native loader/QC precache helpers already support assets.

Smallest adapter: extend the existing stock-liquid driver with a q30 boots mode;
prepare one artifact through the actual installed QC spawn function, then let
actual world Think/contact dispatch grant the ability. Reuse generated inputs,
queue/native dispatch, complete snapshots/parser and replay. Prepared entity
placement/timed artifact properties/resources are explicit fixture seams,
not a claim about finding an unmodified shipped-map pickup. Do not call
artifact_touch directly or set the player's moditems/boots lifetime/selected
flag to manufacture the result. No replacement ability implementation, transport,
QC bootstrap, solver, scheduler or production test API is justified.

## Stages and exact ownership

1. Add -q30boots in tests/stock_liquid_native_fixture.c. Use existing bootstrap
   and actual normal admission for -defaultselection, and the existing native
   configuration for a separate reference process. Prepare the artifact and
   assets with installed spawn QC, retain its scheduled setup/contact and run
   bounded actual world frames until ready. Set only explicitly documented item
   properties/placement, not player ability outputs. Resource preparation may
   precede the existing client resource copy so the parser knows actual models.
2. Generate/receive actual private commands and publish complete production
   snapshots. Demonstrate ordinary replay before pickup, actual contact grant,
   ability-native snapshots, at least one actual airborne charge/jump, authored
   expiration and ordinary replay return. Assert received sequence completion,
   queue retirement, alive selected owner, public native peer visibility/motion.
   Never equate a permission bit with a successful client replay result.
3. Run separate initialized native and selected processes with deterministic
   inputs/item setup. Compare meaningful position/velocity, flags, charge,
   ownership and expiry observations under stated tolerance and explicit clock
   limits; do not partially rewind a world and call it full gameplay parity.
   Inspect exact QC branches if the initialized comparison diverges. Only a
   demonstrated production defect permits a revised, committed narrow fix plan.
4. Document recipes, executed aggregate markers and prepared/captured proof
   limits in tests/README.md and this record. Final local Astra reviews the
   implemented ownership and claims, including simplification and false-positive
   assertions. Consolidate focused Linux checks after the coherent changes.

Exact implementation write set: tests/stock_liquid_native_fixture.c only.
Existing negotiation_native.make is reused, excluding sv_phys.o/cl_main.o with
existing network/skin/QC wrappers. Main owns this plan/index/parent status and
README integration. A single web worker may own that fixture; it is not alone,
must preserve other edits, and cannot touch user-dirty docs/migration-2.0.md or
main. No production write is authorized by this fixture contract. If assets,
setup or a native reference is unavailable, report evidence rather than broaden
into a new framework or claim staged state is pickup proof.

## Acceptance and retained goal

Require real offer/spawn/begin, generated receipt, installed spawn/setup/contact
QC, actual boots ownership/airborne action/expiry, full send/parser authority and
actual replay before/after, exact controlled completion and native reference.
Keep delivery capture/client signon resources/prepared item and synthetic input
limits explicit. A failed actual pickup must fail the fixture, not switch to a
manual grant. Tests should fail if the current eligibility/native boundary is
removed; a bounded temporary countercheck may establish that sensitivity.

Ladder, grapple, other abilities/teleports, complete wet/ledge/map progression,
AD/Mjolnir identities and cooperative-QC/local/load support remain required in
the parent goal. This slice does not shrink or complete that goal. Reopen before
new movement ownership, lifetime policy, protocol or unrelated production edits.
