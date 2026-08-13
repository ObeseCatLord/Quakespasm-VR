#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STRAIGHT_DIR="${1:?usage: install_coop_server_assets.sh STRAIGHT_DIR}"
SERVER_CFG_SRC="$SCRIPT_DIR/codex_coop_server.cfg"
SERVER_CFG_DST="$STRAIGHT_DIR/id1/codex_coop_server.cfg"
DWELL_WEAPONS_SRC="$SCRIPT_DIR/dwellv2p2/vr_weapons.txt"
DWELL_WEAPONS_DST="$STRAIGHT_DIR/dwellv2p2/vr_weapons.txt"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
TMP_FILES=()

cleanup() {
	local f

	for f in "${TMP_FILES[@]}"; do
		rm -f "$f"
	done
}
trap cleanup EXIT

install_with_backup() {
	local src="$1"
	local dst="$2"

	mkdir -p "$(dirname "$dst")"
	if [ -f "$dst" ] && ! cmp -s "$src" "$dst"; then
		cp "$dst" "$dst.bak-codex-deploy-$TIMESTAMP"
	fi
	install -m 0644 "$src" "$dst"
}

install_script_with_backup() {
	local src="$1"
	local dst="$2"

	mkdir -p "$(dirname "$dst")"
	if [ -f "$dst" ] && ! cmp -s "$src" "$dst"; then
		cp "$dst" "$dst.bak-codex-deploy-$TIMESTAMP"
	fi
	install -m 0755 "$src" "$dst"
}

new_tmp() {
	local tmp

	tmp="$(mktemp)"
	TMP_FILES+=("$tmp")
	printf '%s\n' "$tmp"
}

write_server_script() {
	local script_name="$1"
	local game="$2"
	local map="$3"
	local extra_exec="${4:-}"
	local tmp

	tmp="$(new_tmp)"
	{
		printf '%s\n' '#!/bin/sh'
		printf '%s\n' 'SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"'
		printf '%s\n' 'cd "$SCRIPT_DIR" || exit 1'
		printf '%s\n' 'export LD_LIBRARY_PATH="$SCRIPT_DIR:${LD_LIBRARY_PATH:-}"'
		printf '\n'
		printf '%s\n' '# Shared co-op/net settings live in id1/codex_coop_server.cfg.'
		if [ -n "$game" ]; then
			printf 'exec ./quakespasm-openvr.bin -dedicated 16 -condebug -game %s \\\n' "$game"
		else
			printf '%s\n' 'exec ./quakespasm-openvr.bin -dedicated 16 -condebug \'
		fi
		printf '%s\n' '    +exec codex_coop_server.cfg \'
		if [ -n "$extra_exec" ]; then
			printf '    +exec %s \\\n' "$extra_exec"
		fi
		if [ -n "$map" ]; then
			printf '    +map %s \\\n' "$map"
		fi
		printf '%s\n' '    "$@"'
	} > "$tmp"

	install_script_with_backup "$tmp" "$STRAIGHT_DIR/$script_name"
}

rewrite_stale_network_defaults() {
	local cfg="$1"
	local tmp
	local rc

	tmp="$(new_tmp)"
	set +e
	awk '
	function unquote(value) {
		gsub(/"/, "", value)
		return value
	}
	function rewritten(name, value) {
		printf "%s \"%s\"\n", name, value
		changed = 1
	}
	{
		name = $1
		value = unquote($2)
		if (name == "cl_portpingprobe_enable" && (value + 0) != 0) {
			rewritten(name, "0")
			next
		}
		if (name == "cl_netfps" ||
		    name == "cl_extrapolate" ||
		    name == "cl_extrapolate_adaptive" ||
		    name == "cl_extrapolate_adaptive_max" ||
		    name == "cl_extrapolate_adaptive_time" ||
		    name == "cl_net_lerpbuffer" ||
		    name == "cl_net_lerpbuffer_adaptive" ||
		    name == "cl_net_lerpbuffer_adaptive_max" ||
		    name == "cl_net_lerpbuffer_adaptive_time") {
			changed = 1
			next
		}
		if (name == "cl_predictmove" && (value + 0) != 1) {
			rewritten(name, "1")
			next
		}
		if (name == "cl_nopred" && (value + 0) != 0) {
			rewritten(name, "0")
			next
		}
		if (name == "host_maxfps" && (value + 0) == 72) {
			rewritten(name, "250")
			next
		}
		if (name == "host_framerate" && (value + 0) != 0) {
			rewritten(name, "0")
			next
		}
		if (name == "host_timescale" && (value + 0) != 0) {
			rewritten(name, "0")
			next
		}
		if (name == "sys_ticrate" && (value + 0) != 0.025) {
			rewritten(name, "0.025")
			next
		}
		if (name == "sv_maxpacketsize" && (value + 0) != 1400) {
			rewritten(name, "1400")
			next
		}
		if (name == "sv_netsort" && (value + 0) != 1) {
			rewritten(name, "1")
			next
		}
		if (name == "sv_nqplayerphysics" && (value + 0) != 0) {
			rewritten(name, "0")
			next
		}
		if (name == "sv_trustedmovement" && (value + 0) != 1) {
			rewritten(name, "1")
			next
		}
		if (name == "sv_predict_nqmovement" && (value + 0) != 0) {
			rewritten(name, "0")
			next
		}
		if (name == "sv_pmove_legacy_preserve_qc_velocity" && (value + 0) != 1) {
			rewritten(name, "1")
			next
		}
		if (name == "sv_gameplayfix_spawnbeforethinks" && (value + 0) != 0) {
			rewritten(name, "0")
			next
		}
		if (name == "sv_inputtimeout" && (value + 0) != 0) {
			rewritten(name, "0")
			next
		}
		if (name == "sv_replacement_maxpackets" && (value + 0) != 0) {
			rewritten(name, "0")
			next
		}
		if (name == "net_lagdebug" && (value + 0) != 0) {
			rewritten(name, "0")
			next
		}
		if (name == "net_sameip_stale_timeout" && (value + 0) != 3.0) {
			rewritten(name, "3.0")
			next
		}
		if (name == "sv_gravity" && (value + 0) != 800) {
			rewritten(name, "800")
			next
		}
		if (name == "max_edicts" && (value + 0) < 15000) {
			rewritten(name, "15000")
			next
		}
		print
	}
	END {
		exit changed ? 2 : 0
	}
	' "$cfg" > "$tmp"
	rc=$?
	set -e

	if [ "$rc" -eq 0 ]; then
		return
	fi
	if [ "$rc" -ne 2 ]; then
		exit "$rc"
	fi
	cp "$cfg" "$cfg.bak-codex-netdefaults-$TIMESTAMP"
	install -m 0644 "$tmp" "$cfg"
}

scrub_stale_network_defaults() {
	local cfg

	while IFS= read -r cfg; do
		rewrite_stale_network_defaults "$cfg"
	done <<EOF
$(find "$STRAIGHT_DIR" -maxdepth 2 -type f -name '*.cfg' | sort)
EOF
}

install_with_backup "$SERVER_CFG_SRC" "$SERVER_CFG_DST"
install_with_backup "$DWELL_WEAPONS_SRC" "$DWELL_WEAPONS_DST"
scrub_stale_network_defaults

write_server_script "start_id1_server.sh" "" "start"
write_server_script "start_ad_server.sh" "ad" "start"
write_server_script "start_alk_server.sh" "alk" "start"
write_server_script "start_dedicated_server.sh" "enyo" "start"
write_server_script "start_dopa_server.sh" "dopa" "start"
write_server_script "start_enyo_server.sh" "enyo" "start"
write_server_script "start_hipnotic_server.sh" "hipnotic" "start"
write_server_script "start_honey_server.sh" "honey" "start"
write_server_script "start_qbj3_server.sh" "qbj3" "start"
write_server_script "start_ravenkeep_server.sh" "ravenkeep" "start"
write_server_script "start_rogue_server.sh" "rogue" "start"
write_server_script "start_server_rogue.sh" "rogue" "start"
write_server_script "start_smej2_server.sh" "smej2" "start"
write_server_script "start_tombofthunder_server.sh" "tombofthunder" "start"
write_server_script "start_udob_server.sh" "udob" "start"
write_server_script "start_vr_server.sh" "vr" "start"
write_server_script "start_mg1_server.sh" "mg1" "mgend" "server_start.cfg"
