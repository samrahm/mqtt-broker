#!/bin/bash
PORT=1884

# 1. Start a 'Watcher' to see if the Will appears
mosquitto_sub -h localhost -p $PORT -t "status/device1" &
WATCHER_PID=$!

sleep 1

# 2. Connect a client with a Will, then KILL it instantly (ungraceful)
mosquitto_sub -h localhost -p $PORT -t "data" \
  --will-topic "status/device1" \
  --will-payload "OFFLINE_CRASHED" &
DEVICE_PID=$!

sleep 1
echo "Simulating crash of device..."
kill -9 $DEVICE_PID 

# 3. Wait to see if Watcher receives "OFFLINE_CRASHED"
sleep 2
kill $WATCHER_PID