# Local release workflow

Use `Packaging/Release/release.py` for builds, package verification, deployment
and publication. It calls the existing native Linux/ARM recipes, Windows
MSBuild adapter and R2 publisher. It does not use GitHub Actions or install game
data. Run on the Linux release host from the `2.0` checkout with Python 3.11 or
newer. Windows compilation runs inside WinBoat through its private adapter.

## Once per build machine

Provide Docker on the native x86-64 host, a working `foundry` SSH alias for
native ARM64 builds, and the existing WinBoat private SSH tools for Windows.
The Windows guest needs the accepted Visual Studio, Vulkan SDK and Steam Audio
prerequisites recorded by its dependency reference. No adapter installs or
changes those tools globally.

For publication, authenticate `gh` and configure the existing rclone remote.
Credentials remain in their normal tool stores. Put only paths and target
options in `Packaging/Release/local.json`, which is ignored by git. Do not put
tokens, keys or passwords in the release config.

Copy `Packaging/Release/config.example.json` to `Packaging/Release/local.json`
and set the machine paths. `windows.baseline_release` names the accepted Windows
release directory containing its pinned dependency/reference manifests. Those
describe prerequisite SDKs and libraries; engine objects are never reused.
Keep that accepted directory available. New C files and shader items are read
from the current project; changing compiler/link policy or pinned dependencies
requires deliberate requalification rather than silently accepting a different
build environment. See [the Windows adapter](../Packaging/Release/windows/README.md)
for its exact input and verification contract.

Use a release work directory outside the source and installed-game folders.
Each cohort contains one immutable `archives/product.tar`, `entry.json`, a full
source manifest and platform packages/receipts. The engine commit and archive
checksum identify its inputs; a newer automation commit does not relabel it.

## Normal release

Finish implementation and relevant tests, commit on `2.0`, and push that engine
revision. Use the CLI's `run` command with the selected platform targets and
explicit deployment/publication options. `--help` lists the commands and flags.
The command reuses successful, verified targets when resumed. Platform build
recipes retain their compiler, feature, license and portable-library checks.

For example, after setting `output_base` to `/path/to/release-work`:

```sh
python3 Packaging/Release/release.py \
  --config Packaging/Release/local.json \
  --root /path/to/release-work/engine-revision \
  run --revision HEAD --platforms linux,arm,windows \
  --build --stage --deploy --publish-r2 --publish-gh
```

Omit action flags you do not want. A build-only run is:

```sh
python3 Packaging/Release/release.py \
  --config Packaging/Release/local.json \
  --root /path/to/release-work/engine-revision \
  run --revision HEAD --platforms linux,arm,windows --build
```

Staging/publication requires all three platform packages. Individual builds and
verification can select fewer platforms. Commands are sequential so a failed
check stops subsequent publication actions. The normal recipes build clean;
there is no new generalized dependency cache.

Windows builds run once at the end, using a fresh guest source namespace and
native Release MSBuild. Native process exit status and independent source,
compile/output hashes must pass before its files can enter publication. Neither
build success nor dedicated startup certifies Windows/Frame/headset graphics.

## Resume and existing packages

Use the same cohort directory to resume. Every accepted target must still match
its source identity and complete runtime inventory. A success-looking JSON file
alone is insufficient. Conflicting or partially built output is retained for
inspection and rejected. After inspecting its evidence, move only the failed
platform directory outside the cohort and rerun the same command; completed
verified platforms are retained. Never overwrite a differing immutable runtime
or revision namespace. Guest workspace paths are recorded, but recovery from a
transport disconnect is currently explicit rather than automatic.

Explicit verification/import can adopt an already-qualified cohort, including
the Peril/desktop-wheel release at
`ad198cf7b27d04bfa10f87478e9dd1745e8bb28f`, without rebuilding it for
automation-only changes. Publication is stricter: the existing R2 publisher
requires the engine revision to equal the current tip of `origin/2.0`. Finish
publishing before pushing later documentation or automation commits. Do not
reset the branch to make an old cohort publishable.

To verify an existing cohort without rebuilding or publishing:

```sh
python3 Packaging/Release/release.py \
  --config Packaging/Release/local.json \
  --root /path/to/release-work/existing-cohort \
  verify --revision FULL_ENGINE_COMMIT --platforms linux,arm,windows
```

To bring qualified packages into a fresh managed cohort, use `import` with
`--from-root /path/to/existing-cohort` and an explicit `--revision`. The original
cohort remains intact. Preserve its already-created stage when present.

## Deployment and publication boundaries

Straight deployment verifies and copies the entire matching Linux runtime to an
immutable per-revision directory, then replaces only its two engine entry
points. Executable-only backups are retained. Existing wrapper hashes are
checked; mods, saves and configs remain external. A second identical deployment
verifies and skips without a new backup.

Foundry's ARM build creates a private temporary workspace and retrieves its
verified package. It does not restart the live server or change its mod. Server
administration is a separate explicitly requested operation.

R2 staging preserves dependency source-access archives and component notices.
Publication verifies immutable objects and public bytes before exposing updater
metadata. Keep an existing stage intact for retry: regenerating compressed
archives can produce different bytes within the same immutable revision.

GitHub publication replaces only the three runtime archives and `SHA256SUMS`.
Public copies omit top-level README files while preserving every other archive
member, executable, library and required component notice. The private staged
archives stay immutable. Public archive hashes therefore differ from private
archive hashes; `SHA256SUMS` must describe the actual GitHub downloads.
Keep the original `v2.0.0` tag when replacing its attachments, and include links
to the exact engine commit in the notes. Automatic tag source downloads describe
the tag, not necessarily the replaced runtime assets. Do not attach product or
dependency source-access archives to GitHub.

By default, GitHub publication preserves the release description and updates its
current engine/source links. Set the optional `github.notes_file` to an absolute
path when supplying a revised description; it must contain source ZIP/TAR links
for the exact engine commit and no conflicting older source links. Public notes
must not expose private distribution endpoints or private application details;
both generated and supplied notes are checked before publication.

To remove public documentation from an already-published qualified build after
an automation/docs-only commit, use the explicit refresh operation:

```sh
python3 Packaging/Release/release.py \
  --config Packaging/Release/local.json \
  --root /path/to/release-work/existing-cohort \
  publish --github --refresh-existing
```

This preserves the engine revision and release tag. It requires the existing
publication receipt and matching GitHub release identity and asset digests;
it cannot publish an unrelated build. Normal new-build publication still
requires the exact engine revision at the tip of `origin/2.0`. No new engine
build or private distribution update is needed for public documentation cleanup.

## Results

The original cross-platform recipes have already built and qualified the Peril
and desktop-wheel engine update. Automation verification and the bounded Astra
review dispositions are recorded in
[the implementation plan](release-automation-2.0-plan.md). End checks for the
coordinator use that exact cohort and isolated failure fixtures; no new engine
build is needed solely for these scripts and instructions.

The mod-browser fix at `c02ef0bf9fb78f989a6612909371a07addd5bfee` also
qualified fresh Linux, native ARM64 and Windows builds through the coordinator.
Straight deployment preserved existing wrappers and backed up only engine entry
points. The first Windows attempt stopped before compilation because first-use
PowerShell module progress contaminated its JSON workspace response. Suppressing
progress for that response keeps native errors fatal; an injected-progress check
returned clean JSON. The failed namespace was retained, and a fresh Windows
attempt built and passed independent source, compiler and artifact verification.
The matching packages were published to the R2 `2.0` channel and the existing
GitHub `v2.0.0` release; public objects, updater metadata and release attachment
hashes passed the coordinator's verification.

## Public download connection recovery

The VR presentation cohort exposed two interrupted public reads: a TLS EOF at
connection setup and a reset while reading. Keep the existing publication owner,
immutable objects and hash gating. Retry only a failed public download, up to
four attempts with 1/2/4-second delays, reopening from byte zero and computing a
fresh digest. Certificate verification and HTTP failures remain fatal; a
completed wrong hash is not retried. Both build-object and updater-metadata
readbacks use the same small helper. Do not regenerate staged archives or
rebuild engines for this automation correction.

The eight focused transport checks and the existing 19 coordinator and nine
public-archive/publication checks passed after this correction.

The presentation engine at `d939ec2f4695c55b5f86cc4c07e0e668c7374e9f`
qualified fresh Linux x64, native Linux ARM64 and Windows Release builds. The
matching Linux runtime was deployed with the existing wrapper hashes preserved.
The same staged bytes were published to the isolated 2.0 channel and the existing
GitHub `v2.0.0` release. Public byte hashes and metadata passed verification;
GitHub contains exactly the three runtime packages and `SHA256SUMS`, with root
READMEs omitted and license notices retained. Two interrupted public reads
recovered within the bounded helper during the successful publication attempt.
This later automation/documentation commit does not change that released engine
revision and requires no engine rebuild.
