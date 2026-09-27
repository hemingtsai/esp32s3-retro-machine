#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
    printf 'usage: test_vscode.sh assembler build-directory\n' >&2
    exit 2
fi

ASM=$1
BUILD=$2
SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
REPO=$(CDPATH= cd "$SCRIPT_DIR/.." && pwd)
EXTENSION="$REPO/editors/vscode-retrolsp"
TEST_ROOT="$BUILD/vscode-tests"
mkdir -p "$TEST_ROOT"

if ! command -v node > /dev/null 2>&1; then
    printf 'SKIP(vscode) node is not available\n'
    exit 0
fi

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

for source in client.js convert.js extension.js test/run.js test/extension.js; do
    if ! node --check "$EXTENSION/$source" 2> "$TEST_ROOT/syntax.log"; then
        printf 'FAIL(syntax) %s\n' "$source"
        cat "$TEST_ROOT/syntax.log"
        exit 1
    fi
done

for manifest in package.json language-configuration.json syntaxes/retro-c.tmLanguage.json; do
    if ! python3 -m json.tool "$EXTENSION/$manifest" > /dev/null 2>&1; then
        printf 'FAIL(json)   %s\n' "$manifest"
        exit 1
    fi
done

if ! python3 -c "import json,sys
manifest = json.load(open(sys.argv[1]))
language = manifest['contributes']['languages'][0]
grammar = manifest['contributes']['grammars'][0]
assert language['id'] == 'retro-c', language
assert language['extensions'] == ['.rc'], language
assert grammar['language'] == language['id'], grammar
assert grammar['scopeName'] == 'source.retro-c', grammar
for key in ('retro-c.serverPath', 'retro-c.compilerPath', 'retro-c.buildOnStartup',
            'retro-c.debounce', 'retro-c.trace'):
    assert key in manifest['contributes']['configuration']['properties'], key
for key in ('retro-c.restartServer', 'retro-c.buildTools', 'retro-c.showOutput'):
    assert any(command['command'] == key for command in manifest['contributes']['commands']), key
assert manifest['engines']['vscode'].startswith('^1.'), manifest['engines']
" "$EXTENSION/package.json" 2> "$TEST_ROOT/manifest.log"; then
    printf 'FAIL(manifest) package.json does not describe the extension\n'
    cat "$TEST_ROOT/manifest.log"
    exit 1
fi

if ! timeout 120 node "$EXTENSION/test/run.js" "$LSP" "$RETROCC" "$REPO" \
    > "$TEST_ROOT/report.out" 2> "$TEST_ROOT/report.err"; then
    printf 'FAIL(run)    vscode extension\n'
    cat "$TEST_ROOT/report.err"
    cat "$TEST_ROOT/report.out"
    exit 1
fi
if grep -q '^FAIL' "$TEST_ROOT/report.out"; then
    printf 'FAIL(run)    vscode extension\n'
    grep '^FAIL' "$TEST_ROOT/report.out"
    exit 1
fi
cat "$TEST_ROOT/report.out"

if ! timeout 240 node "$EXTENSION/test/extension.js" "$REPO" \
    > "$TEST_ROOT/activation.out" 2> "$TEST_ROOT/activation.err"; then
    printf 'FAIL(run)    vscode activation\n'
    cat "$TEST_ROOT/activation.err"
    cat "$TEST_ROOT/activation.out"
    exit 1
fi
if grep -q '^FAIL' "$TEST_ROOT/activation.out"; then
    printf 'FAIL(run)    vscode activation\n'
    grep '^FAIL' "$TEST_ROOT/activation.out"
    exit 1
fi
cat "$TEST_ROOT/activation.out"
rm -rf "$TEST_ROOT"
