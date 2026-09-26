#!/bin/sh
set -e
cd "$(dirname "$0")/.."

if [ "$#" -ne 2 ]; then
    printf 'usage: test_examples.sh ASSEMBLER BUILD_DIR\n'
    exit 2
fi

ASSEMBLER=$1
BUILD_DIR=$2
RETROCC=${RETROCC:-$BUILD_DIR/retrocc}
RUNNER=$BUILD_DIR/compiler-runner
EXAMPLES_DIR=examples/retro-c

if [ ! -x "$RETROCC" ]; then
    if ! cc -std=c11 -Wall -Wextra -Werror -Wpedantic tools/retrocc.c -o "$BUILD_DIR/retrocc" 2> "$BUILD_DIR/retrocc.log"; then
        printf 'FAIL(build) retrocc for examples\n'
        cat "$BUILD_DIR/retrocc.log"
        exit 1
    fi
fi

if [ ! -x "$RUNNER" ]; then
    if ! cc -std=c11 -Wall -Wextra -I main tests/compiler_runner.c -o "$BUILD_DIR/compiler-runner" 2> "$BUILD_DIR/compiler-runner.log"; then
        printf 'FAIL(build) compiler-runner for examples\n'
        cat "$BUILD_DIR/compiler-runner.log"
        exit 1
    fi
fi

WORK_DIR=$BUILD_DIR/examples
rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"
status=0

check_example() {
    name=$1
    expected_rs=$2
    expected_halted=$3
    output_file=$WORK_DIR/$name.out
    if ! "$RETROCC" "$EXAMPLES_DIR/$name.rc" -o "$WORK_DIR/$name.asm" > "$output_file" 2>&1; then
        printf 'FAIL(compile) %s\n' "$name"
        cat "$output_file"
        status=1
        return
    fi
    if ! "$ASSEMBLER" "$WORK_DIR/$name.asm" -o "$WORK_DIR/$name.bin" >> "$output_file" 2>&1; then
        printf 'FAIL(assemble) %s\n' "$name"
        cat "$output_file"
        status=1
        return
    fi
    if ! "$RUNNER" "$WORK_DIR/$name.bin" >> "$output_file" 2>&1; then
        printf 'FAIL(run)      %s\n' "$name"
        cat "$output_file"
        status=1
        return
    fi
    line=$(grep '^RS=' "$output_file")
    if [ -z "$line" ]; then
        printf 'FAIL(state)    %s\n' "$name"
        cat "$output_file"
        status=1
        return
    fi
    actual_rs=${line#*RS=}
    actual_rs=${actual_rs%% *}
    actual_halted=${line##*HALTED=}
    actual_halted=${actual_halted%% *}
    if [ "$actual_rs" != "$expected_rs" ] || [ "$actual_halted" != "$expected_halted" ]; then
        printf 'FAIL(result)   %s expected RS=%s HALTED=%s got %s\n' \
            "$name" "$expected_rs" "$expected_halted" "$line"
        status=1
        return
    fi
    printf 'PASS(example)  %s %s\n' "$name" "$actual_rs"
}

check_example add 002A 1
check_example array_scan 272A 1
check_example bitwise 2C48 1
check_example bounds_guard FFFF 1
check_example fibonacci 0037 1
check_example prime_sum 004D 1
check_example side_effects 0C00 1
check_example simple_sort 001F 1

rm -rf "$WORK_DIR"
exit $status
