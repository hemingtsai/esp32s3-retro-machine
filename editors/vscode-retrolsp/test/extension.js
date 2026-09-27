'use strict';

const assert = require('assert');
const fs = require('fs');
const Module = require('module');
const os = require('os');
const path = require('path');

const results = [];
let failures = 0;

function check(name, actual, expected) {
    const left = JSON.stringify(actual);
    const right = JSON.stringify(expected);
    if (left === right) {
        results.push(`PASS(activation)  ${name}`);
        return;
    }
    results.push(`FAIL(activation)  ${name} expected ${right} got ${left}`);
    failures += 1;
}

function checkTrue(name, value) {
    check(name, Boolean(value), true);
}

class Disposable {
    dispose() {}
}

class Position {
    constructor(line, character) {
        this.line = line;
        this.character = character;
    }
}

class Range {
    constructor(startLine, startCharacter, endLine, endCharacter) {
        this.start = new Position(startLine, startCharacter);
        this.end = new Position(endLine, endCharacter);
    }
}

class Uri {
    constructor(value) {
        this.value = value;
    }

    toString() {
        return this.value;
    }

    static parse(value) {
        return new Uri(value);
    }
}

function makeMock(settings) {
    const state = {
        settings,
        diagnostics: new Map(),
        openHandlers: [],
        changeHandlers: [],
        closeHandlers: [],
        providers: {},
        commands: new Map(),
        output: []
    };
    const mock = {
        Uri,
        Position,
        Range,
        Disposable,
        MarkdownString: class {
            constructor(value) {
                this.value = value;
            }
        },
        Hover: class {
            constructor(contents) {
                this.contents = contents;
            }
        },
        CompletionItem: class {
            constructor(label, kind) {
                this.label = label;
                this.kind = kind;
            }
        },
        SymbolInformation: class {
            constructor(name, kind, range, selectionRange) {
                this.name = name;
                this.kind = kind;
                this.range = range;
                this.selectionRange = selectionRange;
            }
        },
        SignatureInformation: class {
            constructor(label, documentation, ...parameters) {
                this.label = label;
                this.parameters = parameters;
            }
        },
        ParameterInformation: class {
            constructor(label) {
                this.label = label;
            }
        },
        SignatureHelp: class {
            constructor() {
                this.signatures = [];
                this.activeSignature = 0;
                this.activeParameter = 0;
            }
        },
        DocumentSemanticTokensLegend: class {
            constructor(tokenTypes, tokenModifiers) {
                this.tokenTypes = tokenTypes;
                this.tokenModifiers = tokenModifiers;
            }
        },
        workspace: {
            get textDocuments() {
                return [];
            },
            getConfiguration(section) {
                return {
                    get(key, fallback) {
                        const name = `${section}.${key}`;
                        return Object.prototype.hasOwnProperty.call(state.settings, name)
                            ? state.settings[name]
                            : fallback;
                    }
                };
            },
            onDidOpenTextDocument(handler) {
                state.openHandlers.push(handler);
                return new Disposable();
            },
            onDidChangeTextDocument(handler) {
                state.changeHandlers.push(handler);
                return new Disposable();
            },
            onDidCloseTextDocument(handler) {
                state.closeHandlers.push(handler);
                return new Disposable();
            }
        },
        languages: {
            createDiagnosticCollection() {
                return {
                    set(uri, items) {
                        state.diagnostics.set(uri.toString(), items);
                    },
                    dispose() {
                        state.diagnostics.clear();
                    }
                };
            },
            registerHoverProvider(selector, provider) {
                state.providers.hover = provider;
                return new Disposable();
            },
            registerDocumentSymbolProvider(selector, provider) {
                state.providers.symbol = provider;
                return new Disposable();
            },
            registerCompletionItemProvider(selector, provider) {
                state.providers.completion = provider;
                return new Disposable();
            },
            registerSignatureHelpProvider(selector, provider) {
                state.providers.signature = provider;
                return new Disposable();
            },
            registerDocumentSemanticTokensProvider(selector, provider, legend) {
                state.providers.tokens = { provider, legend };
                return new Disposable();
            }
        },
        commands: {
            registerCommand(name, handler) {
                state.commands.set(name, handler);
                return new Disposable();
            }
        },
        window: {
            createOutputChannel() {
                return {
                    appendLine(line) {
                        state.output.push(line);
                    },
                    show() {},
                    dispose() {}
                };
            },
            showErrorMessage() {
                state.error = true;
            },
            showInformationMessage() {
                state.info = true;
            }
        }
    };
    return { mock, state };
}

function wait(milliseconds) {
    return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

function fakeDocument(uri, text, version) {
    return {
        uri: Uri.parse(uri),
        languageId: 'retro-c',
        version: version === undefined ? 1 : version,
        getText() {
            return text;
        }
    };
}

async function main() {
    const repository = process.argv[2];
    const build = fs.mkdtempSync(path.join(os.tmpdir(), 'retrolsp-vscode-'));
    const server = path.join(build, 'retrolsp');
    const compiler = path.join(build, 'retrocc');
    const settings = {
        'retro-c.serverPath': server,
        'retro-c.compilerPath': compiler,
        'retro-c.buildOnStartup': true,
        'retro-c.debounce': 20,
        'retro-c.trace': 'off'
    };
    const { mock, state } = makeMock(settings);
    const original = Module._load;
    Module._load = function patched(request, parent, isMain) {
        if (request === 'vscode') {
            return mock;
        }
        return original.call(this, request, parent, isMain);
    };
    const extension = require('../extension');

    check('repository root', extension.repositoryRoot({
        extensionPath: path.join(repository, 'editors', 'vscode-retrolsp')
    }), repository);

    const context = {
        extensionPath: path.join(repository, 'editors', 'vscode-retrolsp'),
        subscriptions: []
    };
    check('missing tools are reported', extension.missingTools(context).length, 2);
    await extension.activate(context);
    checkTrue('server was built on activation', fs.existsSync(server));
    checkTrue('compiler was built on activation', fs.existsSync(compiler));
    check('no activation error', state.error, undefined);
    check('document sync registered', state.openHandlers.length, 1);
    checkTrue('providers registered',
        Boolean(state.providers.hover && state.providers.symbol && state.providers.completion &&
            state.providers.signature && state.providers.tokens));
    check('semantic token legend matches the server',
        state.providers.tokens.legend.tokenTypes.length, 12);

    const broken = 'u16 main(void)\n{\n    return missing;\n}\n';
    state.openHandlers[0](fakeDocument('file:///demo/broken.rc', broken));
    for (let attempt = 0; attempt < 200 && !state.diagnostics.has('file:///demo/broken.rc');
        attempt += 1) {
        await wait(25);
    }
    const items = state.diagnostics.get('file:///demo/broken.rc');
    checkTrue('diagnostics reach the editor', items !== undefined);
    if (items !== undefined) {
        check('diagnostic count', items.length, 1);
        check('diagnostic message', items[0].message, "unknown identifier 'missing'");
        check('diagnostic severity', items[0].severity, 0);
        check('diagnostic source', items[0].source, 'retrocc');
        check('diagnostic range start', [items[0].range.start.line, items[0].range.start.character],
            [2, 11]);
    }

    state.changeHandlers[0]({ document: fakeDocument('file:///demo/broken.rc', broken, 2) });
    await wait(200);
    const version2 = state.diagnostics.get('file:///demo/broken.rc');
    checkTrue('change keeps the diagnostic', version2 !== undefined);

    const document = fakeDocument('file:///demo/demo.rc',
        fs.readFileSync(path.join(repository, 'tests/compiler/arrays.rc'), 'utf8'));
    state.openHandlers[0](document);
    const symbols = await state.providers.symbol.provideDocumentSymbols(document);
    check('outline names', symbols.map((symbol) => symbol.name), ['pick', 'main']);
    check('outline children', symbols[1].children.map((symbol) => symbol.name),
        ['values', 'index', 'sum']);
    check('outline kind is a function', symbols[0].kind, 11);

    const hover = await state.providers.hover.provideHover(document,
        new Position(11, 12));
    check('hover markdown', hover.contents.value, 'u16 pick(u16 value)');

    const completion = await state.providers.completion.provideCompletionItems(document,
        new Position(8, 10));
    checkTrue('completion labels', completion.some((item) => item.label === 'values'));
    check('completion kind for a function', completion.find((item) => item.label === 'main').kind, 2);

    const signature = await state.providers.signature.provideSignatureHelp(document,
        new Position(11, 16));
    check('signature label', signature.signatures[0].label, 'pick(u16 value)');
    check('signature parameters', signature.signatures[0].parameters.length, 1);
    check('active parameter', signature.activeParameter, 0);

    const tokens = await state.providers.tokens.provider.provideDocumentSemanticTokens(document);
    checkTrue('semantic tokens returned', Array.isArray(tokens.data) && tokens.data.length > 0);

    state.closeHandlers[0](document);
    await extension.deactivate();
    check('diagnostics released', state.diagnostics.size, 0);
    checkTrue('no protocol errors logged',
        !state.output.some((line) => line.includes('protocol error')));
    checkTrue('build messages logged', state.output.some((line) => line.includes('building')));

    fs.rmSync(build, { recursive: true, force: true });
    for (const line of results) {
        process.stdout.write(`${line}\n`);
    }
    if (failures > 0) {
        process.stdout.write(`FAIL(activation)  ${failures} assertion(s)\n`);
        process.exitCode = 1;
        return;
    }
    process.stdout.write('vscode activation tests OK\n');
}

main().catch((error) => {
    process.stdout.write(`${results.join('\n')}\n`);
    process.stderr.write(`driver error: ${error.stack}\n`);
    process.exitCode = 1;
});
