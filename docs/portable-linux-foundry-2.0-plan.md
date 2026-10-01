# C19 slice3: immutable Foundry transport at the existing SSH boundary

2026-10-01. Before-code plan; execute only after all implementation is complete.
Reuse primary scripts/build_linux_arm64_foundry.sh's separate build and retrieval
concept and build_linux_arm64_glibc239.sh's native-architecture/container guard.
Primary scripts are read-only reference, not launch/deployment commands. Drop
their pushed-master requirement, persistent checkout, legacy OpenVR packaging
and installation into a deployed game directory. No server/game modifications.

Only Packaging/Linux/build-foundry.sh, README.md and the minimal Dockerfile COPY
of the stage script/policy. Parallel stage worker owns package.py/host-policy.json;
do not edit those. Established2.0 only; no branch checks/switches.

CLI: build-foundry.sh PRODUCT.tar[.gz] REVISION-40-HEX EMPTY-OUTPUT-DIR [SSH-HOST].
Default SSH alias foundry uses existing SSH configuration. Validate revision,
host argument and empty output before transport. Create a unique fixed-prefix
/tmp directory remotely, require native aarch64 and Docker/git/tar/gzip/hash tools;
do not clone/fetch/check out another repository or alter existing paths. Transfer
one caller-supplied immutable archive/hash. Verify transport hash and its git
archive commit before extracting or building its Dockerfile. Compressed input
decompresses fully to a private file before commit validation, no early-read pipe.

Use the archive's Packaging/Linux Docker context, a unique image tag and native
linux/arm64 runs. Invoke the same builder, then stage in the same provisioned
image from read-only native output and verify the final package read-only. No
host compiler fallback or separate ARM renderer/audio flags. The two architecture
release builds must consume the exact same source archive/revision.

Retrieve the verified package as a tar archive plus checksum: scp -r follows
symlinks and is unsuitable for preserving SONAME aliases. Check retrieval hash
before extraction into the caller's empty output. Retain the unique remote
workspace/image for inspection; report it without destructive cleanup/deployment.
Local scratch cleanup affects only the wrapper-created temporary directory.

README documents the matching native x86-64 Docker/build/stage/verify commands,
ARM wrapper interface, output/receipts/source-access layout and GLIBC<=2.39 plus
recorded compiler-runtime requirements. Host GPU ICD/OpenXR runtime/layers,
audio/display/CA configuration and Quake assets remain prerequisites. Correct
source integration is not tested release readiness. Do not claim standalone
Steam Frame loaded behavior from an ARM build alone.

Expected80–120 lines script+concise README, pause before160 or new deploy logic.
Combined C19 bound1200 remains. No SSH/scripts/builds/probes/tests now; only source
review and scoped diff checks. Final qualification builds and retrieves isolated
ARM client, checks archive identity/aliases/ABI/notices and meaningful installed
software behavior. Windows and user live/performance trials stay deferred.
