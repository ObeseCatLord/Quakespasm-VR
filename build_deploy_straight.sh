#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STRAIGHT_DIR="${STRAIGHT_DIR:-/home/obesecatlord/Windows/Games/quakespasm_straight}"
JOBS="${JOBS:-$(nproc)}"

make -C "$ROOT_DIR/Quake" -f Makefile.linux -j"$JOBS"
install -m 0755 "$ROOT_DIR/Quake/quakespasm-openvr.bin" \
	"$STRAIGHT_DIR/quakespasm-openvr.bin"
"$ROOT_DIR/deploy/install_coop_server_assets.sh" "$STRAIGHT_DIR"
sha256sum "$ROOT_DIR/Quake/quakespasm-openvr.bin" \
	"$STRAIGHT_DIR/quakespasm-openvr.bin"
