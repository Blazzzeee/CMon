#!/usr/bin/env bash

set -euo pipefail
target=test.c

cc -Wall -Wextra -g -o ./target "$target"

echo "Compilation Successfull"
./target
