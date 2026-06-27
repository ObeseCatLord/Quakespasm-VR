#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STRAIGHT_DIR="${STRAIGHT_DIR:-/home/obesecatlord/Windows/Games/quakespasm_straight}"
JOBS="${JOBS:-$(nproc)}"
SERVER_CFG_SRC="$ROOT_DIR/deploy/codex_coop_server.cfg"
SERVER_CFG_DST="$STRAIGHT_DIR/id1/codex_coop_server.cfg"

make -C "$ROOT_DIR/Quake" -f Makefile.linux -j"$JOBS"
install -m 0755 "$ROOT_DIR/Quake/quakespasm-openvr.bin" \
	"$STRAIGHT_DIR/quakespasm-openvr.bin"
mkdir -p "$(dirname "$SERVER_CFG_DST")"
if [ -f "$SERVER_CFG_DST" ] && ! cmp -s "$SERVER_CFG_SRC" "$SERVER_CFG_DST"; then
	cp "$SERVER_CFG_DST" \
		"$SERVER_CFG_DST.bak-codex-deploy-$(date +%Y%m%d-%H%M%S)"
fi
install -m 0644 "$SERVER_CFG_SRC" "$SERVER_CFG_DST"
sha256sum "$ROOT_DIR/Quake/quakespasm-openvr.bin" \
	"$STRAIGHT_DIR/quakespasm-openvr.bin"
