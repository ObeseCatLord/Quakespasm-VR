#!/usr/bin/env bash
# Fetch the freely redistributable Quake 1.06 shareware episode and extract
# id1/pak0.pak to DEST. Both the archive and the pak are checked by SHA-256.
# Needs curl, unzip and bsdtar (libarchive-tools).
set -euo pipefail
dest=$1
url=https://github.com/Jason2Brownlee/QuakeOfficialArchive/raw/f137e27a83ee4c5468da75c3082a417b18ea000e/bin/quake106.zip
zip_sha=ec6c9d34b1ae0252ac0066045b6611a7919c2a0d78a3a66d9387a8f597553239
pak_sha=35a9c55e5e5a284a159ad2a62e0e8def23d829561fe2f54eb402dbc0a9a946af
if [ -f "$dest" ] && echo "$pak_sha  $dest" | sha256sum -c --status; then exit 0; fi
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
curl -fsSL --retry 3 -o "$work/quake106.zip" "$url"
echo "$zip_sha  $work/quake106.zip" | sha256sum -c --quiet
unzip -q "$work/quake106.zip" -d "$work/x"
bsdtar -xf "$work/x/resource.1" -C "$work/x"
pak=$(find "$work/x" -iname pak0.pak | head -1)
echo "$pak_sha  $pak" | sha256sum -c --quiet
mkdir -p "$(dirname "$dest")"
cp "$pak" "$dest"
