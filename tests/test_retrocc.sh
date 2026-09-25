#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
    printf 'usage: test_retrocc.sh assembler build-directory\n' >&2
    exit 2
fi

ASM=$1
BUILD=$2
SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
REPO=$(CDPATH= cd "$SCRIPT_DIR/.." && pwd)
TEST_ROOT="$BUILD/retrocc-tests"
PROGRAMS="$REPO/tests/compiler"
mkdir -p "$TEST_ROOT"

fail() {
    printf 'FAIL retrocc: %s\n' "$1" >&2
    exit 1
}

print_file() {
    while IFS= read -r line || [ -n "$line" ]; do
        printf '%s\n' "$line"
    done < "$1"
}

run_case() {
    name=$1
    expected=$2
    asm="$TEST_ROOT/$name.asm"
    bin="$TEST_ROOT/$name.bin"
    if ! "$RETROCC" "$PROGRAMS/$name.rc" -o "$asm" > "$TEST_ROOT/$name.compile" 2>&1; then
        print_file "$TEST_ROOT/$name.compile" >&2
        fail "compile $name"
    fi
    if ! "$ASM" "$asm" -o "$bin" > "$TEST_ROOT/$name.assemble" 2>&1; then
        print_file "$TEST_ROOT/$name.assemble" >&2
        fail "assemble $name"
    fi
    if ! output=$("$BUILD/compiler-runner" "$bin" 2>&1); then
        printf '%s\n' "$output" >&2
        fail "execute $name"
    fi
    case "$output" in
        "RS=$expected "*" HALTED=1 FAULT=0 "*) ;;
        *)
            printf '%s\n' "$output" >&2
            fail "$name expected RS=$expected"
            ;;
    esac
}

expect_failure() {
    expected=$1
    source=$2
    output=$3
    rm -f "$output"
    if message=$("$RETROCC" "$source" -o "$output" 2>&1); then
        status=0
    else
        status=$?
    fi
    if [ "$status" -ne 1 ]; then
        printf '%s\n' "$message" >&2
        fail "expected compiler exit 1 for $source"
    fi
    case "$message" in
        *"$expected"*) ;;
        *)
            printf '%s\n' "$message" >&2
            fail "missing diagnostic: $expected"
            ;;
    esac
    if [ -e "$output" ]; then
        fail "failed compilation created $output"
    fi
}

if [ -n "${RETROCC:-}" ]; then
    :
else
    RETROCC="$BUILD/retrocc"
    cc -std=c11 -Wall -Wextra -Werror -Wpedantic "$REPO/tools/retrocc.c" -o "$RETROCC"
fi
cc -std=c11 -Wall -Wextra -Werror "$REPO/tests/compiler_runner.c" -o "$BUILD/compiler-runner"

run_case arithmetic 0096
run_case calls 002E
run_case control 0012
run_case globals 0010
run_case comparisons 05FF
run_case short_circuit 0001
run_case void 0007
run_case scopes 0000
run_case while 0005
run_case stack_guard 8400

printf '%s\n' 'u16 main(void) { return missing; }' > "$TEST_ROOT/unknown.rc"
printf '%s\n' 'u16 add(u16 a, u16 b) { return a + b; }' 'u16 main(void) { return add(1); }' > "$TEST_ROOT/arity.rc"
printf '%s\n' 'void nothing(void) { }' 'u16 main(void) { return nothing(); }' > "$TEST_ROOT/void-value.rc"
printf '%s\n' 'u16 main(void) { u16 value; u16 value; return value; }' > "$TEST_ROOT/duplicate-local.rc"
printf '%s\n' 'u16 main(void) { 1 = 2; return 1; }' > "$TEST_ROOT/lvalue.rc"
printf '%s\n' 'u16 main(void) { return 70000; }' > "$TEST_ROOT/range.rc"
printf '%s\n' 'u16 main(void) { return 2 * 3; }' > "$TEST_ROOT/multiply.rc"
printf '%s\n' 'u16 value;' 'u16 value;' 'u16 main(void) { return value; }' > "$TEST_ROOT/duplicate-global.rc"
printf '%s\n' 'u16 main(u16 value) { return value; }' > "$TEST_ROOT/main-signature.rc"
printf '%s\n' 'u16 main(void) { break; return 0; }' > "$TEST_ROOT/break.rc"
printf '%s\n' 'void value;' 'u16 main(void) { return value; }' > "$TEST_ROOT/void-global.rc"
printf '%s\n' 'u16 main(void) { { u16 hidden = 7; } return hidden; }' > "$TEST_ROOT/expired-local.rc"
printf '%s\n' 'u16 foo(void) { return 1; }' 'u16 FOO(void) { return 2; }' 'u16 main(void) { return foo(); }' > "$TEST_ROOT/case-collision.rc"
printf '%s\n' 'u16 main(void) { return 0; }' '/*/' > "$TEST_ROOT/comment.rc"
printf '%s\n' 'u16 f(u16 a) { return a; }' 'u16 main(void) { return f(1,2,3,4,5,6,7); }' > "$TEST_ROOT/argument-limit.rc"
printf '%s\n' 'void touch(void) { }' 'u16 main(void) { for (touch(); 0; ) { } return 0; }' > "$TEST_ROOT/for-init-void.rc"
printf '%s\n' 'void touch(void) { }' 'u16 main(void) { for (; 0; touch()) { } return 0; }' > "$TEST_ROOT/for-step-void.rc"
python3 - "$TEST_ROOT/deep.rc" "$TEST_ROOT/nul.rc" "$TEST_ROOT/frame.rc" "$TEST_ROOT/flat.rc" "$TEST_ROOT/assign.rc" "$TEST_ROOT/call.rc" <<'PY'
import sys
from pathlib import Path
Path(sys.argv[1]).write_text("u16 main(void) { return " + "(" * 300 + "1" + ")" * 300 + "; }\n")
Path(sys.argv[2]).write_bytes(b"u16 main(void) { return 0; }\x00")
Path(sys.argv[3]).write_text("u16 main(void) {" + "".join(f"u16 v{i};" for i in range(8193)) + "return 0; }\n")
Path(sys.argv[4]).write_text("u16 main(void) { return " + "+".join(["1"] * 100000) + "; }\n")
Path(sys.argv[5]).write_text("u16 main(void) { u16 x; return " + "x=" * 100000 + "1; }\n")
Path(sys.argv[6]).write_text("u16 f(u16 x) { return x; } u16 main(void) { return " + "f(" * 300 + "1" + ")" * 300 + "; }\n")
PY

expect_failure "error: unknown identifier 'missing'" "$TEST_ROOT/unknown.rc" "$TEST_ROOT/unknown.out"
expect_failure "function 'add' expects 2 arguments, got 1" "$TEST_ROOT/arity.rc" "$TEST_ROOT/arity.out"
expect_failure 'u16 function cannot return void' "$TEST_ROOT/void-value.rc" "$TEST_ROOT/void-value.out"
expect_failure "duplicate local 'value'" "$TEST_ROOT/duplicate-local.rc" "$TEST_ROOT/duplicate-local.out"
expect_failure 'assignment target is not a modifiable u16 lvalue' "$TEST_ROOT/lvalue.rc" "$TEST_ROOT/lvalue.out"
expect_failure "integer constant '70000' is outside 0..65535" "$TEST_ROOT/range.rc" "$TEST_ROOT/range.out"
expect_failure "operator '*' is not supported" "$TEST_ROOT/multiply.rc" "$TEST_ROOT/multiply.out"
expect_failure "duplicate symbol 'value'" "$TEST_ROOT/duplicate-global.rc" "$TEST_ROOT/duplicate-global.out"
expect_failure 'main must have signature u16 main(void)' "$TEST_ROOT/main-signature.rc" "$TEST_ROOT/main-signature.out"
expect_failure 'break is only valid inside a loop' "$TEST_ROOT/break.rc" "$TEST_ROOT/break.out"
expect_failure 'global variables must have type u16' "$TEST_ROOT/void-global.rc" "$TEST_ROOT/void-global.out"
expect_failure "unknown identifier 'hidden'" "$TEST_ROOT/expired-local.rc" "$TEST_ROOT/expired-local.out"
expect_failure "duplicate symbol 'FOO'" "$TEST_ROOT/case-collision.rc" "$TEST_ROOT/case-collision.out"
expect_failure 'unterminated block comment' "$TEST_ROOT/comment.rc" "$TEST_ROOT/comment.out"
expect_failure 'a call may pass at most 6 arguments' "$TEST_ROOT/argument-limit.rc" "$TEST_ROOT/argument-limit.out"
expect_failure 'for initializer cannot produce void' "$TEST_ROOT/for-init-void.rc" "$TEST_ROOT/for-init-void.out"
expect_failure 'for step cannot produce void' "$TEST_ROOT/for-step-void.rc" "$TEST_ROOT/for-step-void.out"
expect_failure 'expression nesting exceeds 256 levels' "$TEST_ROOT/deep.rc" "$TEST_ROOT/deep.out"
expect_failure 'expression nesting exceeds 256 levels' "$TEST_ROOT/flat.rc" "$TEST_ROOT/flat.out"
expect_failure 'expression nesting exceeds 256 levels' "$TEST_ROOT/assign.rc" "$TEST_ROOT/assign.out"
expect_failure 'expression nesting exceeds 256 levels' "$TEST_ROOT/call.rc" "$TEST_ROOT/call.out"
expect_failure 'source contains NUL' "$TEST_ROOT/nul.rc" "$TEST_ROOT/nul.out"
expect_failure 'function stack frame exceeds 8192 words' "$TEST_ROOT/frame.rc" "$TEST_ROOT/frame.out"

if "$RETROCC" --help > "$TEST_ROOT/help.out" 2>&1; then
    :
else
    fail "help did not return zero"
fi
if "$RETROCC" --bad > "$TEST_ROOT/bad-cli.out" 2>&1; then
    fail "invalid CLI option succeeded"
else
    status=$?
    if [ "$status" -ne 2 ]; then
        fail "invalid CLI option returned $status"
    fi
fi

printf 'retrocc tests OK\n'
