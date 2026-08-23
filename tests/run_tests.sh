#!/bin/sh
# Host-side test runner for the retro machine simulator.
#
# Usage:
#   sh tests/run_tests.sh
#
# Unit tests compile main.c directly with the host compiler.
# End-to-end tests assemble programs/*.asm with tools/assembler.py,
# load the resulting words into the simulator and assert on final state.
set -e
cd "$(dirname "$0")/.."

BUILD_DIR=$(mktemp -d)
trap 'rm -rf "$BUILD_DIR"' EXIT

CFLAGS="-std=c11 -Wall -Wextra -I main"
status=0

for source in tests/test_*.c; do
  name=$(basename "$source" .c)
  if ! cc $CFLAGS "$source" -o "$BUILD_DIR/$name" 2> "$BUILD_DIR/$name.log"; then
    echo "FAIL(build) $name"
    cat "$BUILD_DIR/$name.log"
    status=1
    continue
  fi
  if ! enable_trace=1 "$BUILD_DIR/$name" > "$BUILD_DIR/$name.out" 2>&1; then
    echo "FAIL(run)    $name"
    cat "$BUILD_DIR/$name.out"
    status=1
  else
    tail -n 1 "$BUILD_DIR/$name.out"
  fi
done

exit $status
