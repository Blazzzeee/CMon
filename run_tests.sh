#!/bin/bash
set -e

echo "Building server..."
pkill target || true
# Ensure dependencies are met. Assumes pkg-config and libs are present.
gcc -O0 -Wall -Wextra -g -o target main.c utils.c commands.c auth.c arena.c $(pkg-config --cflags --libs libevent openssl)

echo "Starting server in background (logs -> server.log)..."
./target > /dev/null 2> server.log &
SERVER_PID=$!

# Give it a moment to bind port
sleep 1

echo "Running tests..."
python3 test_server.py
TEST_EXIT_CODE=$?

echo "Stopping server..."
kill $SERVER_PID
wait $SERVER_PID 2>/dev/null || true

echo "--- Server Logs ---"
cat server.log
echo "-------------------"

if [ $TEST_EXIT_CODE -eq 0 ]; then
    echo "Tests Successful!"
    exit 0
else
    echo "Tests Failed!"
    exit 1
fi
