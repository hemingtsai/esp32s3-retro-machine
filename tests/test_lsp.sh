#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
    printf 'usage: test_lsp.sh assembler build-directory\n' >&2
    exit 2
fi

ASM=$1
BUILD=$2
SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
REPO=$(CDPATH= cd "$SCRIPT_DIR/.." && pwd)
TEST_ROOT="$BUILD/lsp-tests"
CLIENT="$REPO/tests/lsp_client.py"
mkdir -p "$TEST_ROOT"

if [ -n "${RETROCC:-}" ]; then
    :
else
    RETROCC="$BUILD/retrocc"
    cc -std=c11 -Wall -Wextra -Werror -Wpedantic "$REPO/tools/retrocc.c" -o "$RETROCC"
fi

if [ -n "${LSP:-}" ]; then
    :
else
    LSP="$BUILD/retrolsp"
    if ! cc -std=c11 -Wall -Wextra -Werror -Wpedantic "$REPO/tools/retrolsp.c" -o "$LSP" \
        2> "$TEST_ROOT/build.log"; then
        printf 'FAIL(build) retrolsp\n'
        cat "$TEST_ROOT/build.log"
        exit 1
    fi
fi

if "$LSP" --help > "$TEST_ROOT/help.out" 2>&1; then
    :
else
    printf 'FAIL(lsp) help did not return zero\n'
    exit 1
fi
if "$LSP" --bad > "$TEST_ROOT/bad.out" 2>&1; then
    printf 'FAIL(lsp) invalid option succeeded\n'
    exit 1
else
    status=$?
    if [ "$status" -ne 2 ]; then
        printf 'FAIL(lsp) invalid option returned %s\n' "$status"
        exit 1
    fi
fi

run_scenario() {
    name=$1
    report="$TEST_ROOT/$name.report"
    if ! timeout 120 python3 "$CLIENT" --server "$LSP" --compiler "$RETROCC" --root "$REPO" \
        --debounce 20 "$REPO/tests/lsp/$name.json" > "$report" 2> "$TEST_ROOT/$name.log"; then
        printf 'FAIL(run)    lsp %s\n' "$name"
        cat "$TEST_ROOT/$name.log"
        cat "$report"
        exit 1
    fi
}

field() {
    report=$1
    key=$2
    while IFS= read -r line; do
        case "$line" in
            "$key="*)
                printf '%s' "${line#"$key"=}"
                return 0
                ;;
        esac
    done < "$report"
    return 1
}

expect() {
    name=$1
    key=$2
    expected=$3
    if ! actual=$(field "$TEST_ROOT/$name.report" "$key"); then
        printf 'FAIL(lsp)    %s: missing %s\n' "$name" "$key"
        exit 1
    fi
    if [ "$actual" != "$expected" ]; then
        printf 'FAIL(lsp)    %s: %s expected [%s] got [%s]\n' "$name" "$key" "$expected" "$actual"
        exit 1
    fi
}

expect_absent() {
    name=$1
    key=$2
    if field "$TEST_ROOT/$name.report" "$key" > /dev/null; then
        printf 'FAIL(lsp)    %s: unexpected %s\n' "$name" "$key"
        exit 1
    fi
}

run_scenario arrays
expect arrays initialize.capabilities.positionEncoding utf-16
expect arrays initialize.capabilities.textDocumentSync 1
expect arrays initialize.capabilities.hoverProvider true
expect arrays initialize.capabilities.definitionProvider true
expect arrays initialize.capabilities.documentSymbolProvider true
expect arrays initialize.capabilities.completionProvider.triggerCharacters[2] .
expect arrays initialize.capabilities.signatureHelpProvider.triggerCharacters[1] ,
expect arrays initialize.capabilities.semanticTokensProvider.legend.tokenTypes[0] type
expect arrays initialize.capabilities.semanticTokensProvider.legend.tokenTypes[11] invalid
expect arrays initialize.capabilities.semanticTokensProvider.full true
expect arrays initialize.serverInfo.name retrolsp
expect arrays diagnostic[0].count 0
expect arrays documentSymbol[0].name pick
expect arrays documentSymbol[0].kind 12
expect arrays documentSymbol[0].detail 'u16 pick(u16 value)'
expect arrays documentSymbol[0].range.end.line 3
expect arrays documentSymbol[0].children[0].name value
expect arrays documentSymbol[1].name main
expect arrays documentSymbol[1].children[0].name values
expect arrays documentSymbol[1].children[1].name index
expect arrays documentSymbol[1].children[2].name sum
expect_absent arrays documentSymbol[2].name
expect arrays hover.contents.value 'u16 pick(u16 value)'
expect arrays hover.range.start.line 11
expect arrays hover.range.start.character 11
expect arrays hoverLocal.contents.value 'u16 index'
expect arrays hoverParam.contents.value 'u16 value'
expect arrays hoverKeyword None
expect arrays definition.range.start.line 0
expect arrays definition.range.start.character 4
expect arrays completion.isIncomplete false
expect arrays completion.items[0].label u16
expect arrays completion.items[8].label return
expect arrays completion.items[9].label pick
expect arrays completion.items[9].kind 3
expect arrays completion.items[10].label main
expect arrays completion.items[11].label values
expect arrays completion.items[11].detail 'u16 values[4]'
expect_absent arrays completion.items[14]
expect arrays signature.signatures[0].label 'pick(u16 value)'
expect arrays signature.signatures[0].parameters[0].label value
expect arrays signature.activeParameter 0
expect arrays unknown.error.code -32601
expect arrays unknown.error.message 'method not found'
expect arrays exitCode 0
expect arrays tokens.count 113
expect arrays 'tokens[0].line' 0
expect arrays 'tokens[0].character' 0
expect arrays 'tokens[0].length' 3
expect arrays 'tokens[0].type' 0
expect arrays 'tokens[1].character' 4
expect arrays 'tokens[1].type' 3

run_scenario diagnostics
expect diagnostics diagnostic[0].count 1
expect diagnostics diagnostic[0].item[0].severity 1
expect diagnostics diagnostic[0].item[0].source retrocc
expect diagnostics diagnostic[0].item[0].message "unknown identifier 'missing'"
expect diagnostics diagnostic[0].item[0].start.line 2
expect diagnostics diagnostic[0].item[0].start.character 11
expect diagnostics diagnostic[0].item[0].end.character 18
expect diagnostics diagnostic[1].count 1
expect diagnostics diagnostic[1].item[0].message "expected expression, found ';'"
expect diagnostics diagnostic[1].item[0].start.line 2
expect diagnostics diagnostic[1].item[0].start.character 14
expect diagnostics diagnostic[2].uri file:///lsp-fixed.rc
expect diagnostics diagnostic[2].count 0
expect diagnostics diagnostic[3].uri file:///lsp-fixed.rc
expect diagnostics diagnostic[3].count 0
expect diagnostics closed.error.code -32602
expect diagnostics closed.error.message 'unknown document'
expect diagnostics exitCode 0

run_scenario utf16
expect utf16 diagnostic[0].count 0
expect utf16 symbols[0].name main
expect utf16 hover None
expect utf16 exitCode 0
expect utf16 tokens.count 11
expect utf16 'tokens[0].line' 0
expect utf16 'tokens[0].character' 0
expect utf16 'tokens[0].length' 3
expect utf16 'tokens[0].type' 0
expect utf16 'tokens[1].character' 4
expect utf16 'tokens[1].type' 3
expect utf16 'tokens[6].line' 2
expect utf16 'tokens[6].character' 4
expect utf16 'tokens[6].length' 20
expect utf16 'tokens[6].type' 7
expect utf16 'tokens[7].line' 3
expect utf16 'tokens[7].type' 5

run_scenario debounce
expect debounce diagnostic[0].count 1
expect debounce diagnostic[0].item[0].message "unknown identifier 'oops'"
expect debounce diagnostic[0].item[0].start.line 2
expect debounce diagnostic[1].count 0
expect debounce exitCode 0

run_scenario robust
expect robust diagnostic[0].count 0
expect robust symbols[0].name add
expect robust symbols[0].detail 'u16 add(u16 left, u16 right)'
expect robust symbols[0].children[0].name left
expect robust symbols[0].children[1].name right
expect robust hover.contents.value 'u16 left'
expect robust exitCode 0

printf 'retrolsp tests OK\n'
