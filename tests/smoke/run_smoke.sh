#!/usr/bin/env bash
# Runtime smoke tests for a built Linux package, using the shareware pak0.pak.
#
#   run_smoke.sh RUNTIME_DIR PAK0 [MONADO_PREFIX]
#
# RUNTIME_DIR holds bin/vkquake (a staged package or an install tree). Tests:
#   dedicated  dedicated server loads e1m1, changes to e1m2 and e1m3, quits cleanly
#   render     desktop client (-novr) loads e1m1 and keeps rendering for 30 seconds
#   loopback   desktop client joins a co-op dedicated server over UDP and is listed
#   vr         client attaches an OpenXR stereo session to a simulated Monado
#              headset (null compositor) and loads e1m1; needs MONADO_PREFIX
# Graphics use whatever Vulkan driver is present; CI uses Mesa lavapipe on Xvfb.
# The shareware game ignores +commands (the engine only honours them for the
# registered game), so start-up commands go in id1/autoexec.cfg instead.
# SMOKE_ONLY selects a space-separated subset; SMOKE_LOGS keeps the logs.
set -uo pipefail

runtime=$(realpath "$1"); pak=$(realpath "$2"); monado=${3:+$(realpath "$3")}
engine=$runtime/bin/vkquake
[ -x "$engine" ] || { echo "missing $engine" >&2; exit 2; }
tests=${SMOKE_ONLY:-"dedicated render loopback ${monado:+vr}"}
work=$(mktemp -d)
logs=${SMOKE_LOGS:-$work/logs}
mkdir -p "$logs" "$work/base/id1"
ln -s "$pak" "$work/base/id1/pak0.pak"
pids=()
cleanup() {
	for pid in "${pids[@]}"; do kill "$pid" 2>/dev/null; done
	wait 2>/dev/null
	rm -rf -- "$work"
}
trap cleanup EXIT

# Isolate every run from any desktop session, VR runtime or user config.
unset WAYLAND_DISPLAY XDG_SESSION_TYPE XR_RUNTIME_JSON PULSE_SERVER
export SDL_AUDIODRIVER=dummy XDG_CONFIG_HOME=$work/xdg

if [[ " $tests " =~ \ (render|loopback|vr)\  ]]; then
	display=:$((90 + RANDOM % 100))
	Xvfb "$display" -screen 0 1280x720x24 -nolisten tcp >"$logs/xvfb.log" 2>&1 &
	pids+=($!)
	for _ in $(seq 50); do xdpyinfo -display "$display" >/dev/null 2>&1 && break; sleep 0.2; done
	export DISPLAY=$display SDL_VIDEODRIVER=x11
fi

failed=()
pass() { echo "ok   $1"; }
fail() { echo "FAIL $1: $2"; failed+=("$1"); }

# Each engine run gets its own HOME so its config and console log stay separate.
engine_home() { mkdir -p "$work/home-$1"; echo "$work/home-$1"; }
console_log() { echo "$work/home-$1/.local/share/vkQuake/qconsole.log"; }
startup() { printf '%s\n' "$@" >"$work/base/id1/autoexec.cfg"; }

# Wait up to $3 seconds for pattern $2 in file $1.
wait_for() {
	local end=$((SECONDS + $3))
	while ((SECONDS < end)); do
		grep -qE "$2" "$1" 2>/dev/null && return 0
		sleep 1
	done
	return 1
}

# Fatal engine messages that must never appear in a smoke log.
fatal='Host_Error|Sys_Error|SV_Error|Segmentation|Assertion|stack smashing|AddressSanitizer|runtime error:'
check_fatal() {
	if grep -qE "$fatal" "$2" "$3" 2>/dev/null; then
		fail "$1" "fatal error: $(grep -hE "$fatal" "$2" "$3" 2>/dev/null | head -1)"
		return 1
	fi
}

# Dedicated server with its console on a FIFO: start_server NAME PORT, then
# `say_server CMD` and `server_has PATTERN SECONDS`.
start_server() {
	server_name=$1
	server_log=$(console_log "$1"); server_out=$logs/$1.out
	server_fifo=$work/$1.fifo; mkfifo "$server_fifo"
	HOME=$(engine_home "$1") timeout 900 "$engine" -dedicated 4 -condebug -basedir "$work/base" \
		-port "$2" <"$server_fifo" >"$server_out" 2>&1 &
	server_pid=$!; pids+=($server_pid)
	exec {server_fd}>"$server_fifo"
}
say_server() { echo "$1" >&"$server_fd"; }
server_has() { wait_for "$server_log" "$1" "$2"; }
stop_server() {
	say_server quit
	local end=$((SECONDS + 30))
	while kill -0 "$server_pid" 2>/dev/null && ((SECONDS < end)); do sleep 1; done
	kill -0 "$server_pid" 2>/dev/null && { kill "$server_pid"; server_quit=1; } || server_quit=0
	wait "$server_pid" 2>/dev/null
	exec {server_fd}>&-
	cp "$server_log" "$logs/$server_name.log" 2>/dev/null
}

run_dedicated() {
	startup "map e1m1"
	start_server dedicated 26101
	local map ok=1
	for map in e1m1 e1m2 e1m3; do
		[ "$map" = e1m1 ] || say_server "changelevel $map"
		sleep 2; say_server mapname
		server_has "\"mapname\" is \"$map\"" 120 || { ok=0; break; }
	done
	stop_server
	check_fatal dedicated "$server_out" "$server_log" || return
	[ "$ok" = 1 ] || { fail dedicated "never reached $map"; return; }
	[ "$server_quit" = 0 ] || { fail dedicated "did not quit on command"; return; }
	pass dedicated
}

# Software Vulkan is far too slow for a full timedemo on a CI runner, so the
# desktop client loads a map and must keep rendering for 30 seconds.
run_render() {
	local home log out pid
	startup "map e1m1"
	home=$(engine_home render); log=$(console_log render); out=$logs/render.out
	HOME=$home "$engine" -novr -condebug -basedir "$work/base" -window -width 320 -height 240 >"$out" 2>&1 &
	pid=$!; pids+=($pid)
	if ! wait_for "$log" "Slipgate" 300; then
		cp "$log" "$logs/render.log" 2>/dev/null
		check_fatal render "$out" "$log" && fail render "e1m1 did not load"
		kill "$pid" 2>/dev/null; return
	fi
	sleep 30
	kill -0 "$pid" 2>/dev/null || { cp "$log" "$logs/render.log"; fail render "engine exited during play"; return; }
	kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
	cp "$log" "$logs/render.log"
	check_fatal render "$out" "$log" && pass render
}

run_loopback() {
	local home log out pid joined=0
	startup "coop 1" "map e1m1"
	start_server lb-server 26102
	sleep 2; say_server mapname
	if ! server_has '"mapname" is "e1m1"' 120; then stop_server; fail loopback "server did not start"; return; fi
	startup "connect 127.0.0.1:26102"
	home=$(engine_home lb-client); log=$(console_log lb-client); out=$logs/lb-client.out
	HOME=$home "$engine" -novr -condebug -basedir "$work/base" -window -width 320 -height 240 >"$out" 2>&1 &
	pid=$!; pids+=($pid)
	if wait_for "$log" "Slipgate" 300; then
		sleep 5
		say_server status
		server_has "players: 1 active" 30 && joined=1
	fi
	kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
	cp "$log" "$logs/lb-client.log" 2>/dev/null
	stop_server
	check_fatal loopback "$server_out" "$server_log" && check_fatal loopback "$out" "$log" || return
	[ "$joined" = 1 ] || { fail loopback "client never joined the server"; return; }
	pass loopback
}

run_vr() {
	local run home log out mpid pid json
	run=$work/xr; mkdir -p "$run"
	json=$monado/share/openxr/1/openxr_monado.json
	[ -f "$json" ] || { fail vr "no Monado runtime manifest at $json"; return; }
	XDG_RUNTIME_DIR=$run SIMULATED_ENABLE=1 XRT_COMPOSITOR_NULL=1 XRT_NO_STDIN=1 \
		LD_LIBRARY_PATH=$monado/lib "$monado/bin/monado-service" >"$logs/monado.log" 2>&1 &
	mpid=$!; pids+=($mpid)
	if ! wait_for "$logs/monado.log" "service has started" 30; then fail vr "Monado did not start"; return; fi
	startup "map e1m1"
	home=$(engine_home vr); log=$(console_log vr); out=$logs/vr.out
	XDG_RUNTIME_DIR=$run XR_RUNTIME_JSON=$json HOME=$home "$engine" -vr -condebug -basedir "$work/base" \
		-window -width 320 -height 240 >"$out" 2>&1 &
	pid=$!; pids+=($pid)
	if wait_for "$log" "OpenXR stereo session attached" 120 && wait_for "$log" "Slipgate" 300; then
		sleep 10
		if ! kill -0 "$pid" 2>/dev/null; then cp "$log" "$logs/vr.log"; fail vr "engine exited during play"
		else
			kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
			cp "$log" "$logs/vr.log"
			check_fatal vr "$out" "$log" && pass vr
		fi
	else
		cp "$log" "$logs/vr.log" 2>/dev/null
		check_fatal vr "$out" "$log" && fail vr "no OpenXR session or map"
	fi
	kill "$pid" "$mpid" 2>/dev/null
}

for name in $tests; do "run_$name"; done
echo "logs: $logs"
if ((${#failed[@]})); then echo "${#failed[@]} smoke test(s) failed: ${failed[*]}"; exit 1; fi
echo "all smoke tests passed: $tests"
