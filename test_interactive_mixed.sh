#!/bin/bash
# Interactive mixed connectivity test script (VR + Desktop)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
QUAKE_DIR="$SCRIPT_DIR/Quake"

# Cleanup function
cleanup() {
    echo "Shutting down Quake instances..."
    kill $SERVER_PID $CLIENT_DESKTOP_PID 2>/dev/null
    # We leave the /tmp directories for debugging if needed, 
    # but normally they should be cleaned up.
}
trap cleanup EXIT

# 0. Setup VR Environment (OpenComposite to OpenXR via Monado)
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
./quakespasm-openvr -novr -dedicated 8 +coop 1 +map start -port 26000 > "$SCRIPT_DIR/server_interactive.log" 2>&1 &
SERVER_PID=$!
sleep 2

# 2. Start Client Desktop (WindowED)
echo "Starting Client Desktop..."
mkdir -p /tmp/quake_interactive_desktop
# Force desktop client NOT to use VR
VR_OVERRIDE="/tmp/invalid_vr_override" \
HOME=/tmp/quake_interactive_desktop \
./quakespasm-openvr -novr +joystick 0 +cl_startdemos 0 +connect 127.0.0.1:26000 \
-window -width 1024 -height 768 > "$SCRIPT_DIR/client_desktop_interactive.log" 2>&1 &
CLIENT_DESKTOP_PID=$!
sleep 2

# 3. Start Client VR (Foreground)
echo "Starting Client VR..."
mkdir -p /tmp/quake_interactive_vr
# Use LAN IP for VR client to avoid disconnecting desktop client
HOME=/tmp/quake_interactive_vr LD_PRELOAD="$VR_OVERRIDE/vrclient.so" \
./quakespasm-openvr -vr +joystick 0 +cl_startdemos 0 +connect 192.168.0.7:26000

# Client VR exiting triggers cleanup
echo "VR Client exited. Cleaning up..."
