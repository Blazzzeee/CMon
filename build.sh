#!/usr/bin/env bash

echo "Starting build phase"
set -euo pipefail
target=main.c

cc -Wall -Wextra -g -o ./target "$target" $(pkg-config --cflags --libs libevent)

echo "Compilation Successfull"

echo "Starting Server"
./target
