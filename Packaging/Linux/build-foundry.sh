#!/usr/bin/env bash
# Build and retrieve an immutable ARM client without touching the live server.
set -Eeuo pipefail
fail() { printf 'build-foundry: %s\n' "$*" >&2; exit 1; }
[[ $# == 3 || $# == 4 ]] || fail 'usage: build-foundry.sh PRODUCT.tar[.gz] REVISION EMPTY-OUTPUT-DIR [SSH-HOST]'
product=$(realpath -e -- "$1")
revision=${2,,}
host=${4:-foundry}
[[ -f "$product" && "$revision" =~ ^[0-9a-f]{40}$ ]] || fail 'regular archive and 40-hex revision required'
[[ "$host" =~ ^[A-Za-z0-9][A-Za-z0-9_.@:-]*$ ]] || fail 'SSH host must be a simple alias or user@host'
case "$product" in
    *.tar.gz|*.tgz) archive_name=product.tar.gz ;;
    *.tar) archive_name=product.tar ;;
    *) fail 'product must be a tar or tar.gz git archive' ;;
esac
mkdir -p -- "$3"
output=$(realpath -e -- "$3")
[[ -d "$output" ]] || fail 'output is not a directory'
[[ -z "$(find "$output" -mindepth 1 -maxdepth 1 -print -quit)" ]] || fail 'output must be empty'
product_hash=$(sha256sum "$product" | awk '{print $1}')
scratch=$(mktemp -d "${TMPDIR:-/tmp}/vkquake-arm-retrieve.XXXXXX")
trap 'rm -rf -- "$scratch"' EXIT
remote_dir=$(ssh -- "$host" bash -s <<'REMOTE'
set -Eeuo pipefail
[[ "$(uname -m)" == aarch64 ]] || { echo 'native aarch64 host required' >&2; exit 1; }
for tool in docker git tar gzip sha256sum; do command -v "$tool" >/dev/null; done
mktemp -d /tmp/vkquake-2.0-arm64.XXXXXX
REMOTE
)
[[ "$remote_dir" =~ ^/tmp/vkquake-2\.0-arm64\.[A-Za-z0-9]{6}$ ]] || fail 'unexpected remote workspace'
scp -- "$product" "$host:$remote_dir/$archive_name"
ssh -- "$host" bash -s -- "$remote_dir" "$revision" "$archive_name" "$product_hash" <<'REMOTE'
set -Eeuo pipefail
work=$1; revision=$2; archive_name=$3; expected_hash=$4
product="$work/$archive_name"
[[ "$(sha256sum "$product" | awk '{print $1}')" == "$expected_hash" ]]
if [[ "$archive_name" == *.gz ]]; then
    gzip -dc -- "$product" > "$work/source.tar"
else
    cp -- "$product" "$work/source.tar"
fi
[[ "$(git get-tar-commit-id < "$work/source.tar")" == "$revision" ]]
mkdir "$work/src" "$work/native" "$work/package"
tar -xf "$work/source.tar" -C "$work/src"
image="vkquake-2.0-arm64:${revision:0:12}-${work##*.}"
docker build --platform linux/arm64 -t "$image" "$work/src/Packaging/Linux"
docker run --rm --platform linux/arm64 \
    -v "$work:/input:ro" -v "$work/native:/output" \
    "$image" "/input/$archive_name" "$revision" /output
docker run --rm --platform linux/arm64 --entrypoint python3 \
    -v "$work/native:/output:ro" -v "$work/package:/package" \
    "$image" /usr/local/bin/package.py stage /output /package
docker run --rm --platform linux/arm64 --network none --entrypoint python3 \
    -v "$work/package:/package:ro" \
    "$image" /usr/local/bin/package.py verify /package
tar -cf "$work/package.tar" -C "$work/package" .
(cd "$work" && sha256sum package.tar > package.tar.sha256)
REMOTE
scp -- "$host:$remote_dir/package.tar" "$host:$remote_dir/package.tar.sha256" "$scratch/"
(cd "$scratch" && sha256sum --check package.tar.sha256)
tar --no-same-owner --no-same-permissions -xf "$scratch/package.tar" -C "$output"
printf 'ARM client retrieved to %s; transport checksum matched.\n' "$output"
printf 'Remote source/build/package workspace retained: %s\n' "$remote_dir"
