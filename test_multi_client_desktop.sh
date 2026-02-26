#!/bin/bash
# Multi-client connectivity test script (Two Desktop Clients)

# Cleanup function
cleanup() {
    echo "Shutting down Quake instances..."
    kill $SERVER_PID $CLIENT1_PID $CLIENT2_PID 2>/dev/null
    rm -rf /tmp/quake_client1 /tmp/quake_client2
}
trap cleanup EXIT

# 1. Start Server
echo "Starting local server (dedicated) on port 26000..."
./Quake/quakespasm-openvr -basedir ./Quake -novr -dedicated 8 +coop 1 +map start -port 26000 > server.log 2>&1 &
SERVER_PID=$!
sleep 3

# 2. Start Client 1 (Desktop)
echo "Starting Client 1 (Desktop)..."
mkdir -p /tmp/quake_client1
HOME=/tmp/quake_client1 ./Quake/quakespasm-openvr -basedir ./Quake -novr +connect 127.0.0.1:26000 > client1.log 2>&1 &
CLIENT1_PID=$!

# 3. Start Client 2 (Desktop)
echo "Starting Client 2 (Desktop)..."
mkdir -p /tmp/quake_client2
HOME=/tmp/quake_client2 ./Quake/quakespasm-openvr -basedir ./Quake -novr +connect 127.0.0.1:26000 > client2.log 2>&1 &
CLIENT2_PID=$!

echo "Test running. Check logs: server.log, client1.log, client2.log"
echo "Press Ctrl+C to stop."
wait
