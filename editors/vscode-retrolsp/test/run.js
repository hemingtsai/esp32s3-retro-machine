'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const { RetroClient, Transport, encode, initialize } = require('../client');
const convert = require('../convert');

const results = [];
let failures = 0;

function check(name, actual, expected) {
    const left = JSON.stringify(actual);
    const right = JSON.stringify(expected);
    if (left === right) {
        results.push(`PASS(vscode)  ${name}`);
        return;
    }
    results.push(`FAIL(vscode)  ${name} expected ${right} got ${left}`);
    failures += 1;
}

function checkTrue(name, value) {
    check(name, Boolean(value), true);
}

function testTransport() {
    const seen = [];
    const logged = [];
    const transport = new Transport((message) => seen.push(message), (line) => logged.push(line));
    transport.append(Buffer.from('garbage\r\n\r\n', 'ascii'));
    transport.append(Buffer.from('Content-Length: 0\r\nX-Extra: 1\r\n\r\n', 'ascii'));
    const first = encode({ jsonrpc: '2.0', id: 1, result: { ok: true } });
    const second = encode({ jsonrpc: '2.0', method: 'textDocument/publishDiagnostics', params: { uri: 'x' } });
    const split = Math.floor(first.length / 2);
    transport.append(first.subarray(0, split));
    transport.next();
    check('transport waits for a complete body', seen.length, 0);
    transport.append(first.subarray(split));
    transport.append(second);
    transport.next();
    check('transport parses two messages', seen.length, 2);
    check('transport keeps the result', seen[0].result, { ok: true });
    check('transport keeps the notification', seen[1].method,
        'textDocument/publishDiagnostics');
    checkTrue('transport logs the skipped header', logged.some((line) => line.includes('Content-Length')));
}

function testConvert() {
    check('legend matches the server', convert.TOKEN_TYPES.length, 12);
    check('legend has no modifiers', convert.TOKEN_MODIFIERS.length, 0);
    check('severity error', convert.diagnosticSeverity(1), 0);
    check('severity warning', convert.diagnosticSeverity(2), 1);
    check('severity fallback', convert.diagnosticSeverity(99), 0);
    check('range start', convert.rangeOf({ range: { start: { line: 2, character: 5 }, end: { line: 2, character: 9 } } }),
        { startLine: 2, startCharacter: 5, endLine: 2, endCharacter: 9 });
    check('range fallback', convert.rangeOf({}), { startLine: 0, startCharacter: 0, endLine: 0, endCharacter: 0 });
    check('selection range wins', convert.selectionRangeOf({
        range: { start: { line: 0, character: 0 }, end: { line: 3, character: 1 } },
        selectionRange: { start: { line: 0, character: 4 }, end: { line: 0, character: 8 } }
    }), { startLine: 0, startCharacter: 4, endLine: 0, endCharacter: 8 });
    check('completion function', convert.completionItemKind(3), 2);
    check('completion variable', convert.completionItemKind(6), 5);
    check('completion keyword', convert.completionItemKind(14), 13);
    check('completion fallback', convert.completionItemKind(250), 5);
    check('symbol function', convert.symbolKind(12), 11);
    check('symbol variable', convert.symbolKind(13), 12);
    check('hover markdown', convert.hoverText({ contents: { kind: 'markdown', value: 'u16 f()' } }), 'u16 f()');
    check('hover string', convert.hoverText({ contents: 'plain' }), 'plain');
    check('hover null', convert.hoverText(null), null);
    check('hover list', convert.hoverText({ contents: ['a', { value: 'b' }] }), 'a\n\nb');
    const decoded = convert.decodeSemanticTokens([0, 0, 3, 0, 0, 0, 4, 6, 2, 0, 2, 1, 1, 5, 0]);
    check('semantic tokens decode', decoded, [
        { line: 0, character: 0, length: 3, type: 0 },
        { line: 0, character: 4, length: 6, type: 2 },
        { line: 2, character: 1, length: 1, type: 5 }
    ]);
    check('semantic tokens reject garbage', convert.decodeSemanticTokens('nope'), []);
}

function wait(milliseconds) {
    return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

async function testClient(server, compiler, root) {
    const published = [];
    const lines = [];
    const client = new RetroClient({
        command: server,
        args: ['--stdio', '--compiler', compiler, '--debounce', '20'],
        cwd: root,
        onDiagnostics: (params) => published.push(params),
        onLog: (line) => lines.push(line)
    });
    client.start();
    const ready = await initialize(client, root);
    check('initialize reports the server name', ready.serverInfo.name, 'retrolsp');
    check('initialize negotiates utf-16', ready.capabilities.positionEncoding, 'utf-16');
    checkTrue('initialize advertises hover', ready.capabilities.hoverProvider === true);
    checkTrue('initialize advertises semantic tokens',
        ready.capabilities.semanticTokensProvider !== undefined);
    client.notify('initialized', {});

    const source = fs.readFileSync(path.join(root, 'tests/compiler/arrays.rc'), 'utf8');
    const uri = 'file://' + path.join(root, 'tests/compiler/arrays.rc');
    client.notify('textDocument/didOpen', {
        textDocument: { uri, languageId: 'retro-c', version: 1, text: source }
    });

    const symbols = await client.request('textDocument/documentSymbol', {
        textDocument: { uri }
    });
    check('document symbol names', symbols.map((item) => item.name), ['pick', 'main']);
    check('document symbol children', symbols[1].children.map((item) => item.name),
        ['values', 'index', 'sum']);

    const tokens = await client.request('textDocument/semanticTokens/full', {
        textDocument: { uri }
    });
    checkTrue('semantic tokens returned', Array.isArray(tokens.data) && tokens.data.length > 0);
    check('semantic token count is a multiple of five', tokens.data.length % 5, 0);
    const decoded = convert.decodeSemanticTokens(tokens.data);
    checkTrue('semantic tokens decode', decoded.length > 50);
    checkTrue('semantic tokens stay inside the legend',
        decoded.every((token) => token.type < convert.TOKEN_TYPES.length));

    const hover = await client.request('textDocument/hover', {
        textDocument: { uri }, position: { line: 11, character: 12 }
    });
    check('hover text', convert.hoverText(hover), 'u16 pick(u16 value)');
    check('hover range', convert.rangeOf(hover), {
        startLine: 11, startCharacter: 11, endLine: 11, endCharacter: 15
    });

    const definition = await client.request('textDocument/definition', {
        textDocument: { uri }, position: { line: 11, character: 12 }
    });
    check('definition range', convert.rangeOf(definition), {
        startLine: 0, startCharacter: 4, endLine: 0, endCharacter: 8
    });

    const completion = await client.request('textDocument/completion', {
        textDocument: { uri }, position: { line: 8, character: 10 }
    });
    const labels = completion.items.map((item) => item.label);
    checkTrue('completion offers keywords', labels.includes('u16') && labels.includes('return'));
    checkTrue('completion offers functions', labels.includes('pick') && labels.includes('main'));
    checkTrue('completion offers locals', labels.includes('index') && labels.includes('sum'));
    check('completion detail for arrays', completion.items.find((item) => item.label === 'values').detail,
        'u16 values[4]');

    const signature = await client.request('textDocument/signatureHelp', {
        textDocument: { uri }, position: { line: 11, character: 16 }
    });
    check('signature label', signature.signatures[0].label, 'pick(u16 value)');
    check('signature parameter', signature.signatures[0].parameters[0].label, 'value');
    check('signature active parameter', signature.activeParameter, 0);

    const brokenUri = 'file://' + path.join(root, 'build', 'lsp-vscode-broken.rc');
    client.notify('textDocument/didOpen', {
        textDocument: {
            uri: brokenUri, languageId: 'retro-c', version: 1,
            text: 'u16 main(void)\n{\n    return nope;\n}\n'
        }
    });
    const documentSymbols = await client.request('textDocument/documentSymbol', {
        textDocument: { uri: brokenUri }
    });
    check('broken document still reports symbols', documentSymbols[0].name, 'main');
    for (let attempt = 0; attempt < 100 && published.length < 2; attempt += 1) {
        await wait(20);
    }
    const broken = published.find((params) => params.uri === brokenUri);
    checkTrue('diagnostics published for the broken document', broken !== undefined);
    if (broken !== undefined) {
        check('diagnostic count', broken.diagnostics.length, 1);
        check('diagnostic message', broken.diagnostics[0].message, "unknown identifier 'nope'");
        check('diagnostic severity', convert.diagnosticSeverity(broken.diagnostics[0].severity), 0);
        check('diagnostic range', convert.rangeOf(broken.diagnostics[0]), {
            startLine: 2, startCharacter: 11, endLine: 2, endCharacter: 15
        });
    }

    client.notify('textDocument/didClose', { textDocument: { uri: brokenUri } });
    const unknown = await client.request('textDocument/documentSymbol', {
        textDocument: { uri: brokenUri }
    }).then(() => null, (error) => error.message);
    checkTrue('closed document is rejected', unknown !== null && unknown.includes('unknown document'));

    await client.stop();
    check('client stopped cleanly', client.process, null);
    check('stop left no pending requests', client.pending.size, 0);
    checkTrue('no unexpected server errors', !lines.some((line) => line.includes('protocol error')));
}

async function main() {
    const server = process.argv[2];
    const compiler = process.argv[3];
    const root = process.argv[4];
    testTransport();
    testConvert();
    await testClient(server, compiler, root);
    for (const line of results) {
        process.stdout.write(`${line}\n`);
    }
    if (failures > 0) {
        process.stdout.write(`FAIL(vscode)  ${failures} assertion(s)\n`);
        process.exitCode = 1;
        return;
    }
    process.stdout.write('vscode extension tests OK\n');
}

main().catch((error) => {
    process.stdout.write(`${results.join('\n')}\n`);
    process.stderr.write(`driver error: ${error.stack}\n`);
    process.exitCode = 1;
});
