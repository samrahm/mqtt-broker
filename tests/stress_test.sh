#!/bin/bash
BROKER_IP="localhost"
PORT=1884

echo "Starting 10 background subscribers..."
for i in {1..10}
do
   mosquitto_sub -h $BROKER_IP -p $PORT -t "nust/stress" -i "client_$i" & 
done

sleep 2
echo "Sending one message to all 10..."
mosquitto_pub -h $BROKER_IP -p $PORT -t "nust/stress" -m "Mass broadcast successful"

sleep 1
echo "Killing test clients..."
pkill mosquitto_sub