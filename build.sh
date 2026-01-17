#!/usr/bin/env bash

echo "Starting build phase"
set -euo pipefail
target=main.c
DEBUG=${DEBUG:-0}

cc -Wall -Wextra -g -o ./target "$target" $(pkg-config --cflags --libs libevent)

echo "Compilation Successfull"

if [ "$DEBUG" -eq 0 ]; then
    echo "Starting server without debugger"
    ./target
else
    echo "Starting server inside gdb"
    gdb --args ./target
fi
