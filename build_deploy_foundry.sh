#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FOUNDRY_HOST="${FOUNDRY_HOST:-foundry}"
SSH_CONFIG="${SSH_CONFIG:-/home/obesecatlord/.ssh/config}"
REMOTE_BUILD_DIR="${REMOTE_BUILD_DIR:-/tmp/quakespasm-openvr-foundry-build}"
REMOTE_STRAIGHT_DIR="${REMOTE_STRAIGHT_DIR:-/home/ubuntu/quakespasm_straight}"
REMOTE_OPENVR_PKG_CONFIG="${REMOTE_OPENVR_PKG_CONFIG:-/home/ubuntu/quakespasm_build/pc}"
REMOTE_URL="$(git -C "$ROOT_DIR" config --get remote.origin.url)"
COMMIT="$(git -C "$ROOT_DIR" rev-parse HEAD)"

ssh -F "$SSH_CONFIG" "$FOUNDRY_HOST" bash -s -- \
	"$REMOTE_BUILD_DIR" \
	"$REMOTE_STRAIGHT_DIR" \
	"$REMOTE_OPENVR_PKG_CONFIG" \
	"$REMOTE_URL" \
	"$COMMIT" <<'REMOTE_SCRIPT'
set -euo pipefail

build_dir="$1"
straight_dir="$2"
openvr_pkg_config="$3"
remote_url="$4"
commit="$5"

if [ -e "$build_dir" ] && [ ! -d "$build_dir/.git" ]; then
	echo "Remote build path exists but is not a git checkout: $build_dir" >&2
	exit 1
fi

if [ ! -d "$build_dir/.git" ]; then
	git clone "$remote_url" "$build_dir"
fi

cd "$build_dir"
git fetch origin
git checkout -f "$commit"

# The dedicated server never exposes the desktop add-on browser. Foundry has
# the runtime curl library but not its development headers, so keep that
# optional client-only transport disabled in the server binary.
PKG_CONFIG_PATH="$openvr_pkg_config" \
	make -C Quake -f Makefile.linux FLAC_DYNAMIC=1 USE_CURL=0 -j"$(nproc)"

mkdir -p "$straight_dir/codex_binary_backups"
if [ -f "$straight_dir/quakespasm-openvr.bin" ]; then
	cp "$straight_dir/quakespasm-openvr.bin" \
		"$straight_dir/codex_binary_backups/quakespasm-openvr.bin.$(date +%Y%m%d-%H%M%S)"
fi

install -m 0755 Quake/quakespasm-openvr.bin \
	"$straight_dir/quakespasm-openvr.bin"
if [ -x deploy/install_coop_server_assets.sh ]; then
	deploy/install_coop_server_assets.sh "$straight_dir"
fi
file "$straight_dir/quakespasm-openvr.bin"
sha256sum "$straight_dir/quakespasm-openvr.bin"
REMOTE_SCRIPT
