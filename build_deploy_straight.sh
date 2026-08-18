#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STRAIGHT_DIR="${STRAIGHT_DIR:-/home/obesecatlord/Windows/Games/quakespasm_straight}"
JOBS="${JOBS:-$(nproc)}"
CXX_COMMAND="${CXX:-g++ -std=gnu++14}"

# Linux and MinGW builds share object names, so force the requested target to
# be rebuilt even when a Windows verification build ran immediately before it.
make -B -C "$ROOT_DIR/Quake" -f Makefile.linux -j"$JOBS" CXX="$CXX_COMMAND"
install -m 0755 "$ROOT_DIR/Quake/quakespasm-openvr.bin" \
	"$STRAIGHT_DIR/quakespasm-openvr.bin"
install -m 0755 "$ROOT_DIR/Quake/quakespasm-openvr" \
	"$STRAIGHT_DIR/quakespasm-openvr"
install -m 0644 "$ROOT_DIR/Quake/quakespasm.pak" \
	"$STRAIGHT_DIR/quakespasm.pak"
if [ -x "$ROOT_DIR/deploy/install_coop_server_assets.sh" ]; then
	"$ROOT_DIR/deploy/install_coop_server_assets.sh" "$STRAIGHT_DIR"
fi
sha256sum "$ROOT_DIR/Quake/quakespasm-openvr.bin" \
	"$STRAIGHT_DIR/quakespasm-openvr.bin" \
	"$ROOT_DIR/Quake/quakespasm-openvr" \
	"$STRAIGHT_DIR/quakespasm-openvr" \
	"$ROOT_DIR/Quake/quakespasm.pak" \
	"$STRAIGHT_DIR/quakespasm.pak"
