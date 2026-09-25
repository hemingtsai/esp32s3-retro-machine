#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
    printf 'usage: test_assembler.sh executable build-directory\n' >&2
    exit 2
fi

ASM=$1
BUILD=$2
SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
REPO=$(CDPATH= cd "$SCRIPT_DIR/.." && pwd)
TEST_ROOT="$BUILD/assembler-tests"
FIXTURES="$REPO/tests/fixtures/assembler"
mkdir -p "$TEST_ROOT"

fail() {
    printf 'FAIL assembler: %s\n' "$1" >&2
    exit 1
}

print_file() {
    while IFS= read -r line || [ -n "$line" ]; do
        printf '%s\n' "$line"
    done < "$1"
}

assemble() {
    output=$1
    shift
    if ! "$ASM" "$@" -o "$output" > "$TEST_ROOT/command.out" 2>&1; then
        print_file "$TEST_ROOT/command.out" >&2
        fail "assemble failed"
    fi
}

compare() {
    if ! cmp -s "$1" "$2"; then
        diff -u "$2" "$1" || true
        fail "output mismatch: $1"
    fi
}

expect_cli_status() {
    expected=$1
    shift
    if "$@" > "$TEST_ROOT/cli.out" 2>&1; then
        actual=0
    else
        actual=$?
    fi
    if [ "$actual" -ne "$expected" ]; then
        print_file "$TEST_ROOT/cli.out" >&2
        fail "CLI exit status: expected $expected, got $actual"
    fi
}

expect_failure() {
    expected=$1
    source=$2
    output=$3
    if message=$("$ASM" "$source" -o "$output" 2>&1); then
        status=0
    else
        status=$?
    fi
    if [ "$status" -ne 1 ]; then
        printf '%s\n' "$message" >&2
        fail "expected exit 1 for $source"
    fi
    case "$message" in
        *"$expected"*) ;;
        *)
            printf '%s\n' "$message" >&2
            fail "missing error text: $expected"
            ;;
    esac
}

printf '%s\n' '   ' '    ; indented comment' > "$TEST_ROOT/whitespace.asm"
printf '' > "$TEST_ROOT/whitespace.hex"
assemble "$TEST_ROOT/whitespace.actual.hex" "$TEST_ROOT/whitespace.asm" --format hex
compare "$TEST_ROOT/whitespace.actual.hex" "$TEST_ROOT/whitespace.hex"

expect_cli_status 0 "$ASM" -h
expect_cli_status 0 "$ASM" --help
expect_cli_status 2 "$ASM" "$FIXTURES/coverage.asm" -o -h
expect_cli_status 2 "$ASM" "$FIXTURES/coverage.asm" -o --bad
expect_cli_status 2 "$ASM" "$FIXTURES/coverage.asm" --output -h
expect_cli_status 2 "$ASM" "$FIXTURES/coverage.asm" --output --bad
expect_cli_status 2 "$ASM" "$FIXTURES/coverage.asm" --format -h
expect_cli_status 2 "$ASM" "$FIXTURES/coverage.asm" --format --bad
expect_cli_status 2 "$ASM" --bad "$FIXTURES/coverage.asm" -o "$TEST_ROOT/unknown.out"
if ! help_output=$("$ASM" --help 2>&1); then
    fail "help did not return zero"
fi
case "$help_output" in
    usage:*) ;;
    *)
        fail "help output missing usage"
        ;;
esac

if ! python3 "$REPO/tools/assembler.py" "$REPO/tests/programs/loop.asm" -o "$TEST_ROOT/wrapper.hex" --format hex > "$TEST_ROOT/wrapper.log" 2>&1; then
    print_file "$TEST_ROOT/wrapper.log" >&2
    fail "Python wrapper failed"
fi
compare "$TEST_ROOT/wrapper.hex" "$FIXTURES/loop.hex"
if ! PYTHONPATH="$REPO" python3 -c 'import tools.assembler' > "$TEST_ROOT/import.log" 2>&1; then
    print_file "$TEST_ROOT/import.log" >&2
    fail "wrapper import executed main"
fi
mkdir -p "$TEST_ROOT/header-c" "$TEST_ROOT/header-py"
if ! python3 "$REPO/tests/gen_e2e_header.py" "$ASM" "$TEST_ROOT/header-c" > "$TEST_ROOT/header-c.log" 2>&1; then
    print_file "$TEST_ROOT/header-c.log" >&2
    fail "C header generation failed"
fi
if ! python3 "$REPO/tests/gen_e2e_header.py" "$REPO/tools/assembler.py" "$TEST_ROOT/header-py" > "$TEST_ROOT/header-py.log" 2>&1; then
    print_file "$TEST_ROOT/header-py.log" >&2
    fail "Python wrapper header generation failed"
fi
compare "$TEST_ROOT/header-c.log" "$TEST_ROOT/header-py.log"

for name in callret loop memops; do
    assemble "$TEST_ROOT/$name.hex" "$REPO/tests/programs/$name.asm" --format hex
    compare "$TEST_ROOT/$name.hex" "$FIXTURES/$name.hex"
done

for format in hex text addrtext cmd; do
    assemble "$TEST_ROOT/coverage.$format" "$FIXTURES/coverage.asm" --format "$format"
    compare "$TEST_ROOT/coverage.$format" "$FIXTURES/coverage.$format"
done

assemble "$TEST_ROOT/entry.cmd" "$FIXTURES/entry.asm" --format cmd
compare "$TEST_ROOT/entry.cmd" "$FIXTURES/entry.cmd"

assemble "$TEST_ROOT/ldi.hex" "$FIXTURES/ldi.asm" --format hex
compare "$TEST_ROOT/ldi.hex" "$FIXTURES/ldi.hex"

printf '%s\n' '; comment' '# comment' 'nOp ; trailing' > "$TEST_ROOT/comments.asm"
printf '%s\n' 'F000' > "$TEST_ROOT/comments.hex"
assemble "$TEST_ROOT/comments.actual.hex" "$TEST_ROOT/comments.asm" --format hex
compare "$TEST_ROOT/comments.actual.hex" "$TEST_ROOT/comments.hex"

hex_to_binary() {
    python3 - "$1" "$2" <<'PY'
import sys
from pathlib import Path
source = Path(sys.argv[1])
target = Path(sys.argv[2])
words = source.read_text(encoding="ascii").split()
target.write_bytes(b"".join(int(word, 16).to_bytes(2, "little") for word in words))
PY
}

for name in callret loop memops; do
    hex_to_binary "$FIXTURES/$name.hex" "$TEST_ROOT/$name.bin"
    assemble "$TEST_ROOT/$name.actual.bin" "$REPO/tests/programs/$name.asm" --format bin
    compare "$TEST_ROOT/$name.actual.bin" "$TEST_ROOT/$name.bin"
done
hex_to_binary "$FIXTURES/coverage.hex" "$TEST_ROOT/coverage.bin"
assemble "$TEST_ROOT/coverage.actual.bin" "$FIXTURES/coverage.asm" --format bin
compare "$TEST_ROOT/coverage.actual.bin" "$TEST_ROOT/coverage.bin"

if symbols=$("$ASM" "$FIXTURES/coverage.asm" -o "$TEST_ROOT/symbols.out" --format hex --symbols 2>&1); then
    case "$symbols" in
        *"Entry: 0000h"*"Symbols:"*"START"*"FIRST"*"SECOND"*"LABEL"*) ;;
        *)
            printf '%s\n' "$symbols" >&2
            fail "symbol output mismatch"
            ;;
    esac
else
    printf '%s\n' "$message" >&2
    fail "symbol assembly failed"
fi

printf '%s\n' '.word' > "$TEST_ROOT/word-empty.asm"
printf '%s\n' '.word 65536' > "$TEST_ROOT/word-high.asm"
printf '%s\n' '.word -1' > "$TEST_ROOT/word-negative.asm"
printf '%s\n' 'JMP 1' > "$TEST_ROOT/jump-number.asm"
printf '%s\n' 'JMP nowhere' > "$TEST_ROOT/jump-unknown.asm"
printf '%s\n' 'MOV R0, IR' > "$TEST_ROOT/mov-ir.asm"
printf '%s\n' 'LDI 1, PC' > "$TEST_ROOT/ldi-destination.asm"
printf '%s\n' 'BOGUS' > "$TEST_ROOT/unknown.asm"
printf '%s\n' 'x: NOP' 'x: HLT' > "$TEST_ROOT/duplicate-label.asm"
printf '%s\n' 'ADD R0' > "$TEST_ROOT/no-operand.asm"
printf '%s\n' '.entry missing' > "$TEST_ROOT/unknown-entry.asm"
printf '%s\n' '.entry x' '.entry y' > "$TEST_ROOT/duplicate-entry.asm"
printf '%s\n' 'MOV R0' > "$TEST_ROOT/mov-operands.asm"
printf '%s\n' 'LDI 1, IR' > "$TEST_ROOT/ldi-ir.asm"

expect_failure '.word requires a value' "$TEST_ROOT/word-empty.asm" "$TEST_ROOT/word-empty.out"
expect_failure '.word value out of range: 65536' "$TEST_ROOT/word-high.asm" "$TEST_ROOT/word-high.out"
expect_failure '.word value out of range: -1' "$TEST_ROOT/word-negative.asm" "$TEST_ROOT/word-negative.out"
expect_failure 'unknown label: 1' "$TEST_ROOT/jump-number.asm" "$TEST_ROOT/jump-number.out"
expect_failure 'unknown label: nowhere' "$TEST_ROOT/jump-unknown.asm" "$TEST_ROOT/jump-unknown.out"
expect_failure 'IR cannot be a MOV destination' "$TEST_ROOT/mov-ir.asm" "$TEST_ROOT/mov-ir.out"
expect_failure 'LDI destination must be R0-R7' "$TEST_ROOT/ldi-destination.asm" "$TEST_ROOT/ldi-destination.out"
expect_failure 'unknown instruction: BOGUS' "$TEST_ROOT/unknown.asm" "$TEST_ROOT/unknown.out"
expect_failure 'duplicate label: X' "$TEST_ROOT/duplicate-label.asm" "$TEST_ROOT/duplicate-label.out"
expect_failure 'ADD takes no operands' "$TEST_ROOT/no-operand.asm" "$TEST_ROOT/no-operand.out"
expect_failure 'unknown .entry label: MISSING' "$TEST_ROOT/unknown-entry.asm" "$TEST_ROOT/unknown-entry.out"
expect_failure 'duplicate .entry directive' "$TEST_ROOT/duplicate-entry.asm" "$TEST_ROOT/duplicate-entry.out"
expect_failure 'MOV requires source,destination' "$TEST_ROOT/mov-operands.asm" "$TEST_ROOT/mov-operands.out"
expect_failure 'LDI destination must be R0-R7' "$TEST_ROOT/ldi-ir.asm" "$TEST_ROOT/ldi-ir.out"

python3 - "$TEST_ROOT/invalid-utf8.asm" "$TEST_ROOT/nul.asm" "$TEST_ROOT/valid-utf8.asm" <<'PY'
import sys
from pathlib import Path
Path(sys.argv[1]).write_bytes(b"\xff\n")
Path(sys.argv[2]).write_bytes(b"NOP\n\x00HLT\n")
Path(sys.argv[3]).write_bytes("; π\nNOP\n".encode("utf-8"))
PY
expect_failure 'UTF-8' "$TEST_ROOT/invalid-utf8.asm" "$TEST_ROOT/invalid-utf8.out"
expect_failure 'NUL' "$TEST_ROOT/nul.asm" "$TEST_ROOT/nul.out"
printf '%s\n' 'F000' > "$TEST_ROOT/valid-utf8.hex"
assemble "$TEST_ROOT/valid-utf8.actual.hex" "$TEST_ROOT/valid-utf8.asm" --format hex
compare "$TEST_ROOT/valid-utf8.actual.hex" "$TEST_ROOT/valid-utf8.hex"

if "$ASM" "$FIXTURES/coverage.asm" -o "$TEST_ROOT/bad-format.out" --format invalid > "$TEST_ROOT/bad-format.log" 2>&1; then
    fail "invalid format succeeded"
else
    status=$?
    if [ "$status" -ne 2 ]; then
        print_file "$TEST_ROOT/bad-format.log" >&2
        fail "invalid format did not exit 2"
    fi
fi

python3 - "$TEST_ROOT/max.asm" "$TEST_ROOT/over.asm" "$TEST_ROOT/max-label.asm" <<'PY'
import sys
from pathlib import Path
Path(sys.argv[1]).write_text(".word 0\n" * 65536, encoding="ascii")
Path(sys.argv[2]).write_text(".word 0\n" * 65537, encoding="ascii")
Path(sys.argv[3]).write_text(".word 0\n" * 65536 + "tail:\n.entry tail\n", encoding="ascii")
PY
assemble "$TEST_ROOT/max.bin" "$TEST_ROOT/max.asm" --format bin
size=$(wc -c < "$TEST_ROOT/max.bin")
if [ "$size" -ne 131072 ]; then
    fail "64 Ki word boundary size: $size"
fi
expect_failure 'label TAIL address out of 16-bit range: 65536' "$TEST_ROOT/max-label.asm" "$TEST_ROOT/max-label.out"
expect_failure 'program exceeds 64 Ki words' "$TEST_ROOT/over.asm" "$TEST_ROOT/over.out"

printf 'assembler tests OK\n'
