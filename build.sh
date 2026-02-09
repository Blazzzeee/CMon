#!/usr/bin/env bash

echo "Starting build phase"
set -euo pipefail
SRC="main.c auth.c arena.c utils.c commands.c"
DEBUG=${DEBUG:-0}
BIN=target

cc -O0 -Wall -Wextra -g -o "$BIN" $SRC $(pkg-config --cflags --libs libevent openssl)

echo "Compilation Successfull"

if [ "$DEBUG" -eq 0 ]; then
    echo "Starting server without debugger"
    ./"$BIN"
else
    echo "Starting server inside gdb"
    gdb --args ./"$BIN"
fi
