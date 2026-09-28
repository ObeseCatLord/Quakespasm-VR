# q30 jump-boots pickup, native gameplay and replay return

Status: implemented with focused Linux software qualification; final local Astra
review in progress. The plan preceded implementation. Normal q30 session activation is committed
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

## Implemented evidence before final review

The existing driver now prepares an actual QC-spawned artifact before the normal
client resource copy. Thirty-two generated/world warmup frames execute actual
setup while the existing real notarget command prevents early pickup. Then real
notarget0 and prepared artifact placement allow actual world item_touch with
nested artifact_touch. The observation hook records top-level contact without
modifying QC output. No player moditems, charge or expiry is assigned.

Four separate initialized runs pass: native and selected private VR, and native
and selected private desktop. At the default25ms interval all112 frame samples
match ownership/charge/deadline, XYZ position/velocity and flags at printed1e-6
resolution. Pickup is frame0,81 frames own the ability, second airborne jump at
frame4 consumes charge2→1 with vertical velocity280, and actual expiry is frame81.
This is a controlled zero-axis private jump sequence, not general motion parity
or full-world/effect equivalence. Other command intervals are not qualified.

Selected runs require real replay before and after, native/no replay while owned,
exact received-sequence completion and empty queue. Public desktop motion and
visibility remain native in the same world. A temporary source/physics copy with
only boots eligibility1048576 removed from Q30State fails the actual owned-state
native-authority assertion (exit134). That is sensitivity of this boundary, not
an old-server or shipped-map pickup claim. Authoritative source/assets remain
unchanged by the countercheck. Recipes/limits are in tests/README.md.

No production change is required by these results. The requested Luna route was
unavailable; one web worker failed terminally before edits, so main implemented
the coupled fixture locally. All broader parent-goal requirements remain open.

## Initial final-review disposition and strengthened replay checks

Local Astra Max found no production/P1 blocker, but caught P2: replay immediately
after every command is acknowledged can succeed with empty history and zero
preview duration. **Adopted:** reuse LiquidDisposablePreview with an optional
forward-axis mode; assert available authority, positive duration and horizontal
predicted displacement greater than .01. Run it before pickup and after expiry
outside the112-frame comparison, preserving history/baseline/ACK/timers and
restoring pending input/clock. Selected VR/desktop and installed AD runs pass
both positive-preview markers. All four initialized q30 runs remain sampled
equal. This is a disposable client preview, not newly delivered server movement.

Astra's deletion finding was also **adopted:** remove the vacuous warmup contact
count conjunct whose discriminator was not assigned until afterward. No-ownership
still discriminates premature grants. No new production ability case is added.
The follow-up local Astra pass checks that fix alongside the user's reopened
[shared movement reuse decision](predictive-movement-reuse-reassessment-2.0-plan.md);
future work should converge the shared QC adapter rather than grow per-mod gates.
