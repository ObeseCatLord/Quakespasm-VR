#!/bin/bash
# Multi-client mixed connectivity test script (VR + Desktop)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
QUAKE_DIR="$SCRIPT_DIR/Quake"

# Cleanup function
cleanup() {
    echo "Shutting down Quake instances..."
    kill $SERVER_PID $CLIENT_VR_PID $CLIENT_DESKTOP_PID 2>/dev/null
    rm -rf /tmp/quake_client_vr /tmp/quake_client_desktop
}
trap cleanup EXIT

# 0. Setup VR Environment (from test_client.sh)
export VR_OVERRIDE="/home/obesecatlord/Documents/monadoplugins/OpenOVR/build/bin/linux64"
export OVR_FORCE_SYMMETRIC_FOV=1
export XR_RUNTIME_JSON="/usr/share/openxr/1/openxr_monado.json"
export LD_LIBRARY_PATH="$VR_OVERRIDE:$LD_LIBRARY_PATH"
export VR_RUNTIME_PATH="$VR_OVERRIDE"
export OVR_USE_OPENXR=1
export VR_CONFIG_PATH="/home/obesecatlord/.config/openvr"
export VR_LOG_PATH="/home/obesecatlord/.local/share/Steam/logs"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-pipewire}"
export IPC_IGNORE_VERSION=1
export SDL_VIDEODRIVER=x11

# Ensure steam_appid.txt is present
echo "2310" > "$QUAKE_DIR/steam_appid.txt"

cd "$QUAKE_DIR"

# 1. Start Server
echo "Starting local server (dedicated) on port 26000..."
./quakespasm-openvr -novr -dedicated 8 +coop 1 +map start -port 26000 > "$SCRIPT_DIR/server.log" 2>&1 &
SERVER_PID=$!
sleep 3

# 2. Start Client VR
echo "Starting Client VR..."
mkdir -p /tmp/quake_client_vr
HOME=/tmp/quake_client_vr LD_PRELOAD="$VR_OVERRIDE/vrclient.so" \
./quakespasm-openvr -vr +joystick 0 +cl_startdemos 0 +connect 127.0.0.1:26000 > "$SCRIPT_DIR/client_vr.log" 2>&1 &
CLIENT_VR_PID=$!

# 3. Start Client Desktop
echo "Starting Client Desktop..."
mkdir -p /tmp/quake_client_desktop
VR_OVERRIDE="/tmp/invalid_vr_override" \
HOME=/tmp/quake_client_desktop \
./quakespasm-openvr -novr +joystick 0 +cl_startdemos 0 +connect 127.0.0.1:26000 -window -width 1280 -height 720 > "$SCRIPT_DIR/client_desktop.log" 2>&1 &
CLIENT_DESKTOP_PID=$!

echo "Test running. Check logs: server.log, client_vr.log, client_desktop.log"
echo "Killing in 30 seconds for baseline check..."
sleep 30
echo "Test window elapsed."
