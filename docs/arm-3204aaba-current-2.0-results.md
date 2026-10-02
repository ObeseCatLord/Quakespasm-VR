# Current native ARM shipping-source refresh

2026-10-02. Existing build-foundry.sh/build-native.sh/package.py reused unchanged
in a fresh private Foundry work/image namespace. Actual native aarch64 Ubuntu24.04
GCC/G++13.3 build/install/stage/network-disabled verification/checksummed retrieval
and extraction finish0. Original live session followed to terminal exit0; no
restart inferred from observation timeouts. No deployed server/user checkout,
headset/graphics/game/audio device or system runtime changes.

Exact source revision3204aabacf78ddfc0f9814f7b983c87ea90c008f; product SHA256:
a6901fc1a685f80d815b95d5d2b994dbe08729bf147e57f72c6b55366be09f18.
Includes the latest MSVC portability repairs. Subsequent changes so far affect
only docs/tests and do not change shipping inputs. Main compares all697 currently
tracked regular files outside docs/tests against embedded product.tar, including
Nix/workflows/root build inputs: zero missing/different. Manifest/embedded archive
identity/hash agree. Comparison scope explicitly excludes docs/tests; no claim
that later test fixtures are inside this older immutable product archive.

Main ELF is64-bit little-endian, machine183 (AArch64), SHA256:
d009b22e5787b059fd40bfe224d02c3b9b3d552251ac382f75057cf421a06ee9.
Accepted45ELFs/651inventory entries/118Ubuntu contributors. Main independently
reruns the retained staged verifier in an existing owned Ubuntu24 native x86_64
image: read-only package/container, private tmpfs, networknone, only static ELF/
archive/source/dependency/notice checks; terminal0. No ARM binary execution.
Direct Arch-host attempt first exited1 because dpkg-deb is absent; retained as a
host-tool failure, not an artifact defect. No host package installation.

Private receipts/profile and retrieved arm-package:
/home/obesecatlord/FastGames/qsvr-arm-recovery-mgfh0avl/arm3204-current-15oszper.
Actual build/verify argv/logs/status, independent archive/ELF checks, 697-file
comparison and main-reviewed-final-summary.json retained. Remote owned build
workspace /tmp/vkquake-2.0-arm64.AqIGKM remains. The previous32f7f777 result is
[historical](arm-refresh-current-2.0-results.md), not this latest input.

This closes the current ARM compile/package/static-freshness refresh for the
stated shipping archive. Future production repairs require affected refresh;
no native ARM gameplay/headset/provider/audio/performance certification. Windows
full Release/Debug compilation/link remains root-space blocked. Final Linux
shipping refresh/reconciliation, remaining finite feature qualification and
local Astra integration signoff remain F10; whole migration not complete.
