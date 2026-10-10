# Portable native Linux builds

These scripts are the portable native release recipes used by the local
[release workflow](../../docs/release-automation-2.0.md). Use that coordinator
for normal releases and resumable deployment/publication. The lower-level
commands below remain useful for isolated build diagnosis. They do not install
into a game/server directory.

Linux x86-64 and native ARM64 packages have passed the production build,
inventory and dedicated-startup checks recorded in the release results. Those
checks do not certify headset presentation or hardware-specific performance.

Create one committed source archive and use it unchanged for both architectures.
Existing Quake/mod assets are external; no assets are included in the package.
Example native x86-64 route from the repository root (Docker required):

~~~sh
revision=$(git rev-parse HEAD)
work=$(mktemp -d /tmp/vkquake-native-release.XXXXXX)
git archive --format=tar "$revision" > "$work/product.tar"
mkdir "$work/src" "$work/native" "$work/package"
tar -xf "$work/product.tar" -C "$work/src"
image="vkquake-2.0-linux:${revision:0:12}-${work##*.}"
docker build --platform linux/amd64 -t "$image" "$work/src/Packaging/Linux"
docker run --rm --platform linux/amd64 \
  -v "$work:/input:ro" -v "$work/native:/output" \
  "$image" /input/product.tar "$revision" /output
docker run --rm --platform linux/amd64 --entrypoint python3 \
  -v "$work/native:/output:ro" -v "$work/package:/package" \
  "$image" /usr/local/bin/package.py stage /output /package
docker run --rm --platform linux/amd64 --network none --entrypoint python3 \
  -v "$work/package:/package:ro" \
  "$image" /usr/local/bin/package.py verify /package
bash Packaging/Linux/build-foundry.sh "$work/product.tar" "$revision" "$work/arm-package"
~~~

The Foundry wrapper uses the existing SSH alias `foundry`; a fourth argument may
select another native ARM64 host. It creates a unique remote `/tmp` workspace,
builds/stages/verifies in the same provisioned image, and retrieves a tar/checksum
to preserve library aliases. Remote workspaces/images remain for inspection.
It does not use a remote checkout, require a push or change the live server.

`build-native.sh ARCHIVE REVISION EMPTY-DIR` emits `install`, `deps`, `sources`
and `receipts`. `package.py stage BUILD-OUTPUT EMPTY-DIR` creates the runtime
`bin`, `lib`, `share/licenses` plus source access, receipts and an artifact
manifest. `package.py verify DIR` inspects metadata/hashes without executing the
game. Keep the source-access set and manifest alongside distribution artifacts;
it can be distributed as a separate matching archive.

The declared GLIBC ceiling is2.39; actual GLIBCXX/CXXABI requirements are recorded.
Host display/audio services, GPU ICDs, an installed OpenXR runtime/layers and
CA/audio configuration remain prerequisites. Do not set a global LD_LIBRARY_PATH.
Vulkan/OpenXR loaders and application dependencies follow `host-policy.json`.
Windows has a separate native Release adapter in `Packaging/Release/windows`.
Live headset/gaze checks and hardware performance remain user validation.
