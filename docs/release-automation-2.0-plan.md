# Local build and release automation

## Goal and boundary

One local command should prepare an immutable source revision, build the selected
platforms, verify the packages, and optionally deploy or publish them. A resume
command must reuse verified results rather than repeat successful platform builds.
The operator chooses the targets and publication actions. This is a local CLI for
one maintainer; GitHub Actions remain disabled.

## Reuse first

The production references are `Packaging/Linux/build-native.sh`,
`build-foundry.sh`, `package.py`, and `publish-r2.py`, plus the native Windows
MSBuild/verification scripts already used for the qualified release. Keep their
dependency, license, portable-runtime and compiler checks. Add a thin coordinator
and parameterized adapters; do not introduce another dependency manager, build
service, packaging format, or deployment state machine.

The current release cohort at engine revision
`ad198cf7b27d04bfa10f87478e9dd1745e8bb28f` has passed native Linux,
ARM and Windows builds from one archive. Its current working scripts are the
behavioral reference. Private machine paths belong in an ignored local config,
not in tracked defaults. Credentials continue to belong to SSH/rclone/gh.

## Implementation slices

1. Prepare a git archive and file manifest for a committed revision on branch
   `2.0`. Resolve and record the full revision and archive checksum before any
   build. Refuse conflicting output directories.
2. Invoke the existing Linux container recipe and Foundry ARM recipe. Reuse only
   explicitly verified dependency inputs when compatible; a clean recipe remains
   the fallback. Never copy stale engine outputs. Promote the existing Windows
   source handoff, fresh MSBuild and independent verification into an adapter.
3. Resume by verifying source identity and complete package inventories. Import
   an already-qualified cohort explicitly, including the current release, without
   pretending its engine revision equals a newer automation-only commit.
4. Stage/publish with `publish-r2.py`; update only the three runtime archives and
   checksum attachment on the existing GitHub release. Keep source-access
   archives in R2 and retain the existing release tag. Verify public bytes.
5. Deploy a verified Linux runtime to an immutable per-revision directory and
   update only Straight's engine entry points, backing up executable files only.
   Preserve launchers, wrappers, mods, configs and saves. Re-running an identical
   deployment should verify it and make no new backup.
6. Document the runnable commands and required local configuration in `AGENTS.md`
   and a workflow guide. Only branch `2.0` is editable. Foundry builds do not
   restart the live server; deployment there remains an explicit separate action.

## Completion checks

After implementation, exercise CLI preparation and resume/import against the
qualified cohort, prove wrong revisions and modified artifacts are rejected,
inspect deployment idempotency, and run the adapters' syntax/help checks. Do not
rebuild all three platforms solely for automation/documentation changes. Actual
build and publication receipts are the end-to-end reference; dry runs alone do
not establish that an engine build succeeded.

## Review decisions

A bounded Astra review will challenge archive/receipt identity, failure recovery,
dependency reuse invalidation, and publication/deployment ordering. Reusing the
existing recipes is preferred over expanding the coordinator into a second build
system. Review dispositions and verification results will be recorded here.

### Verified environment for the review

| Fact | Evidence / status |
| --- | --- |
| Existing publisher verifies source commit, complete native inventory, Windows file hashes, immutable remote objects and public bytes before exposing updater metadata | Verified in `Packaging/Linux/publish-r2.py` |
| Publishing requires the engine revision to be the exact tip of `origin/2.0` | Verified in `publish()`; finish current publication before pushing automation-only commits |
| Native recipes use one archive, native Ubuntu 24.04 containers, source/dependency receipts, and package verification | Verified in `build-native.sh`, `build-foundry.sh`, `package.py` |
| Foundry wrapper creates a unique remote workspace; it never changes the live server | Verified in `build-foundry.sh` |
| Existing qualified Windows build has fresh source verification, native MSBuild exit status and independent output verification | Verified current cohort receipts; Windows runtime behavior remains untested |
| Current Straight deployment preserves launcher hashes and backs up only two engine executables | Verified current `straight-deployment.json`; coordinator must also support safe identical resume |
| Reusable dependency-cache invalidation is not yet generalized | Unverified; prefer existing clean recipes until a narrowly validated cache is available |

Current lean: file manifests plus existing recipes and explicit actions. Reject a
new task queue/service and custom dependency cache: neither is required to remove
the repeated manual steps. Treat artifact verification, source identity and
publication gating as one concern rather than several redundant state machines.
The review should focus on failure recovery and unsafe resume; do not re-review
renderer, netcode, weapon calibration, or prior platform build qualification.

### Astra disposition

Reviewed with local `gpt-6-astra`, explicit `xhigh`. Astra independently rehashed
the current archive, source manifest, complete native inventories, staged release
and Windows runtime, and checked the existing build and deployment receipts.

| Recommendation | Disposition |
| --- | --- |
| Freeze staged bytes and import the existing stage; gzip regeneration can conflict with immutable uploaded objects | Adopted: stage once, bind its manifest, preserve exact staged bytes on import/resume; promote completed temporary staging |
| Separate historical publication repair from fresh activation | Adapted: keep the existing strict origin-tip gate for this fresh-release workflow; expose partial failures and retain exact local bytes. No implicit rollback or branch reset. Historical reconciliation is a separate future operation |
| Persist the deployment undo record before replacing either engine entry | Adopted: record executable backups, original entry-point state, intended runtime and launcher hashes before replacement; resume only recorded old/new states |
| Preserve successful work on packaging/transport failure | Adapted: verified platform results survive resume; retain failed attempts and remote workspaces for explicit recovery. Avoid a generalized scheduler or automatic live-server action |
| Inventory equality alone does not establish build qualification; delete generalized dependency-cache work | Adopted: bind existing native/Windows qualification evidence on import and keep clean production recipes. No new dependency cache |
| Validate interruption and mismatch paths without another platform rebuild | Adopted: isolated fixtures plus the already-qualified real cohort; scripts/docs do not change the engine |

The principal design correction is preserving exact publication bytes and writing
deployment recovery information before mutation. Those are narrow extensions to
the existing file workflow, not a second packaging or deployment architecture.

## Final verification

- Nineteen coordinator fixtures pass: committed source identity, wrong branch,
  path/output conflicts, changed/added/removed files and symlinks, qualification
  binding, completed-build reuse, stage preservation and interruption before/
  after promotion, process locking, and interrupted/idempotent executable
  deployment. Release-note source links are idempotent.
- Nine Windows offline checks pass against the actual qualified cohort, including
  source/dependency/artifact/receipt mismatch rejection and evolving project item
  inventories. All three reused PowerShell scripts pass native AST syntax parsing
  through private WinBoat; no extra MSBuild or game launch was performed.
- The real CLI imported all three qualified packages and their existing staged
  bytes from engine revision `ad198cf7b27d04bfa10f87478e9dd1745e8bb28f`.
  An actual `run --build --stage --deploy` resumed that managed cohort successfully:
  all platform packages were reused, the existing stage was preserved, and
  Straight deployment returned `changed: false` without another executable backup.
- The original release was published to R2 and GitHub and deployed to Straight.
  Public R2 metadata matched; GitHub's four attached asset hashes/sizes matched,
  the release body matched and its original tag remained unchanged.

No platform was rebuilt solely for automation/docs. The native build receipts
qualify the engine packages; the fixture and real-cohort checks qualify the new
coordinator's import/resume/deployment integration. A completely fresh build
through the newly promoted host adapters has not been repeated during this
automation-only change. Partial platform outputs and remote workspaces are
retained; automatic recovery of a transport disconnect is not implemented.
