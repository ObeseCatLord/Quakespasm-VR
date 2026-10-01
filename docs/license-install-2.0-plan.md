# C18: inherited combined-build and component notices

2026-10-01. Before-code plan. Installation checks remain deferred.

## Reference and evidence

Primary `SPATIAL_AUDIO.md` and its Linux release workflow include the native
GPL2-or-later notice plus `LICENSE-GPL-3.0.txt` for Steam Audio enabled combined
builds, retaining individual component grants. Copy that existing GPL3 text;
do not edit copyright headers or relicense vendored components.

Current Meson installs the executable only. Nix closes over LICENSE.txt and
installs that alone; its enabled-Steam-Audio package metadata still says GPL2+.
The vendored OpenXR headers and mimalloc already carry their own license files.
Native Steam Audio 4.8.1 derivation installs the tagged LICENSE.md but omits its
tagged core/THIRDPARTY.md.

Official notice sources, read directly during planning:
https://raw.githubusercontent.com/ValveSoftware/steam-audio/v4.8.1/LICENSE.md
https://raw.githubusercontent.com/ValveSoftware/steam-audio/v4.8.1/core/THIRDPARTY.md
The primary SDK workflow pins version4.8.1 and the Apache license text SHA256
`cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30`.

## Existing-owner adapter

Add the copied GPL3 notice and a brief component provenance notice. Vendor the
two unmodified official tagged Steam Audio notice texts under LICENSES, retaining
their provenance URLs/version. Install native/base GPL text and combined GPL3
text plus component provenance through Meson's install_data owner. Install
OpenXR/mimalloc component notices under identifiable filenames. When Steam Audio
is actually enabled, install the matching pinned4.8.1 license and third-party
texts. These comprehensive upstream third-party notices do not assert that all
listed optional SDK libraries are linked by the CPU-only build.

Extend the Nix source fileset to those notices; remove its duplicate native-only
license install and identify the Steam Audio enabled combined game as GPL3+.
The SDK derivation separately installs its own exact fetched-tag notices,
using its source path rather than fragile current-directory assumptions.

Do not add another packaging service, dynamic licensing registry or network
download during Meson configure. C19 will reuse this installed notice tree in
portable artifacts and add applicable notices for actually bundled dependencies.
Supported Steam Audio is4.8.1; other SDK versions require matching notices before
distribution. Preserve loader, build features and native desktop behavior.

## Scope and final acceptance

Write set: new LICENSE-GPL-3.0.txt, LICENSE-COMPONENTS.txt, LICENSES Steam Audio
texts, meson.build, flake.nix, nix/steamaudio.nix and native-build documentation.
Source-copy/provenance checks only now. End-of-implementation Linux/ARM install
qualification checks the source closure, optional-SDK install and artifact
notices. C18 source integration does not close portable artifact C19.
