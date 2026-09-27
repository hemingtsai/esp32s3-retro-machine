#!/bin/sh
set -e
cd "$(dirname "$0")/.."

BUILD_DIR=$(mktemp -d)
trap 'rm -rf "$BUILD_DIR"' EXIT

print_file() {
    while IFS= read -r line || [ -n "$line" ]; do
        printf '%s\n' "$line"
    done < "$1"
}

CFLAGS="-std=c11 -Wall -Wextra -I main"
status=0

if ! cc -std=c11 -Wall -Wextra -Werror -Wpedantic tools/assembler.c -o "$BUILD_DIR/assembler" 2> "$BUILD_DIR/assembler.log"; then
    printf 'FAIL(build) assembler\n'
    print_file "$BUILD_DIR/assembler.log"
    exit 1
fi

if ! sh tests/test_assembler.sh "$BUILD_DIR/assembler" "$BUILD_DIR"; then
    printf 'FAIL(run)    assembler\n'
    status=1
fi

if ! sh tests/test_retrocc.sh "$BUILD_DIR/assembler" "$BUILD_DIR"; then
    printf 'FAIL(run)    retrocc\n'
    status=1
fi

if ! sh tests/test_examples.sh "$BUILD_DIR/assembler" "$BUILD_DIR"; then
    printf 'FAIL(run)    examples\n'
    status=1
fi

if ! sh tests/test_lsp.sh "$BUILD_DIR/assembler" "$BUILD_DIR"; then
    printf 'FAIL(run)    retrolsp\n'
    status=1
fi

for source in tests/test_*.c; do
    name=$(basename "$source" .c)
    extra_flags=""
    if [ "$name" = "test_e2e" ]; then
        if ! python3 tests/gen_e2e_header.py "$BUILD_DIR/assembler" "$BUILD_DIR" > "$BUILD_DIR/e2e_programs.h" 2> "$BUILD_DIR/e2e_header.log"; then
            printf 'FAIL(gen)    %s\n' "$name"
            print_file "$BUILD_DIR/e2e_header.log"
            status=1
            continue
        fi
        extra_flags="-I $BUILD_DIR"
    fi
    if [ "$name" = "test_cmdload" ]; then
        if ! "$BUILD_DIR/assembler" tests/programs/loop.asm -o "$BUILD_DIR/loop.cmd" --format cmd > "$BUILD_DIR/cmdload_assembler.out" 2>&1; then
            printf 'FAIL(gen)    %s\n' "$name"
            print_file "$BUILD_DIR/cmdload_assembler.out"
            status=1
            continue
        fi
        extra_flags="-DFILE_PATH=\"$BUILD_DIR/loop.cmd\""
    fi
    if ! cc $CFLAGS $extra_flags "$source" -o "$BUILD_DIR/$name" 2> "$BUILD_DIR/$name.log"; then
        printf 'FAIL(build) %s\n' "$name"
        print_file "$BUILD_DIR/$name.log"
        status=1
        continue
    fi
    if [ "$name" = "test_cmdload" ]; then
        if ! enable_trace=1 "$BUILD_DIR/$name" < "$BUILD_DIR/loop.cmd" > "$BUILD_DIR/$name.out" 2>&1; then
            printf 'FAIL(run)    %s\n' "$name"
            print_file "$BUILD_DIR/$name.out"
            status=1
        else
            printf 'PASS(run)    %s\n' "$name"
        fi
    elif ! enable_trace=1 "$BUILD_DIR/$name" > "$BUILD_DIR/$name.out" 2>&1; then
        printf 'FAIL(run)    %s\n' "$name"
        print_file "$BUILD_DIR/$name.out"
        status=1
    else
        printf 'PASS(run)    %s\n' "$name"
    fi
done

exit $status
