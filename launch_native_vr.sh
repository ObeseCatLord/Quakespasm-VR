#!/bin/bash

# quakespasm-openvr Linux Launch Script
# Use this script to launch the native Linux build with VR enabled.

# Set paths
BINARY="./Quake/quakespasm-openvr"

# Check if binary exists
if [ ! -f "$BINARY" ]; then
    echo "Error: Binary not found at $BINARY"
    echo "Please ensure you have built the project using 'make -f Makefile.linux'"
    exit 1
fi

# Optional: Set OpenXR runtime if using a bridge or if quakespasm-openvr 
# eventually supports OpenXR. For now, it uses OpenVR (SteamVR/Monado).
# export XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_monado.json

# Launch the game with VR enabled
# Pass any additional arguments to the binary
echo "Launching QuakeSpasm-OpenVR..."
"$BINARY" -vr "$@"
