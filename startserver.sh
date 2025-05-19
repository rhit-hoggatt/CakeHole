#!/bin/bash

echo "Starting C DNS server..."
cd releases/v1.0/server || exit
sudo ./server &

echo "Starting web server..."
cd ../web || exit
node server.js &

# Wait for both processes to finish
wait