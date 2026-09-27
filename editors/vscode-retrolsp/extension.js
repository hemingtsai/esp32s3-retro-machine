'use strict';

const fs = require('fs');
const path = require('path');
const { execFile } = require('child_process');
const vscode = require('vscode');
const { RetroClient, initialize } = require('./client');
const convert = require('./convert');

const LANGUAGE_ID = 'retro-c';
const TOOL_SOURCES = ['tools/retrocc.c', 'tools/retrolsp.c'];
let output;
let client;
let diagnostics;
let building;
const disposables = [];

function log(line) {
    if (output !== undefined) {
        output.appendLine(line);
    }
}

function repositoryRoot(context) {
    return path.resolve(context.extensionPath, '..', '..');
}

function resolveSetting(key, fallback) {
    const configuration = vscode.workspace.getConfiguration(LANGUAGE_ID);
    const value = configuration.get(key, '');
    if (typeof value === 'string' && value.trim() !== '') {
        return value;
    }
    return fallback;
}

function toolchain(context) {
    const root = repositoryRoot(context);
    const server = resolveSetting('serverPath', path.join(root, 'build', 'retrolsp'));
    const compiler = resolveSetting('compilerPath', path.join(root, 'build', 'retrocc'));
    return { root, server, compiler };
}

function toolPaths(context) {
    const { root, server, compiler } = toolchain(context);
    return [
        { source: path.join(root, TOOL_SOURCES[0]), output: compiler },
        { source: path.join(root, TOOL_SOURCES[1]), output: server }
    ];
}

function missingTools(context) {
    return toolPaths(context).filter((tool) => !fs.existsSync(tool.output));
}

function run(command, args, cwd) {
    return new Promise((resolve, reject) => {
        execFile(command, args, { cwd, maxBuffer: 4 * 1024 * 1024 }, (error, stdout, stderr) => {
            if (error !== null) {
                reject(new Error(`${stderr.trim() || error.message}`));
                return;
            }
            resolve(stdout);
        });
    });
}

async function buildTools(context) {
    if (building !== undefined) {
        return building;
    }
    const { root } = toolchain(context);
    const tools = toolPaths(context);
    const absent = tools.filter((tool) => !fs.existsSync(tool.source));
    if (absent.length !== 0) {
        throw new Error(
            `cannot find ${absent.map((tool) => path.relative(root, tool.source)).join(' and ')} ` +
            `under ${root}; point retro-c.serverPath and retro-c.compilerPath at existing binaries`);
    }
    fs.mkdirSync(path.join(root, 'build'), { recursive: true });
    building = (async () => {
        for (const tool of tools) {
            if (fs.existsSync(tool.output)) {
                continue;
            }
            log(`building ${path.relative(root, tool.output)}`);
            await run('cc', [
                '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wpedantic', tool.source, '-o', tool.output
            ], root);
        }
    })();
    try {
        await building;
        log('build finished');
    } catch (error) {
        log(`build failed: ${error.message}`);
        throw error;
    } finally {
        building = undefined;
    }
}

function pushDiagnostics(params) {
    if (diagnostics === undefined) {
        return;
    }
    const uri = vscode.Uri.parse(params.uri);
    const items = params.diagnostics || [];
    diagnostics.set(uri, items.map((item) => {
        const range = convert.rangeOf(item);
        return {
            range: new vscode.Range(
                range.startLine, range.startCharacter, range.endLine, range.endCharacter),
            severity: convert.diagnosticSeverity(item.severity),
            source: item.source === undefined ? 'retrocc' : item.source,
            message: item.message === undefined ? '' : item.message
        };
    }));
}

async function startServer(context) {
    const { root, server, compiler } = toolchain(context);
    if (vscode.workspace.getConfiguration(LANGUAGE_ID).get('buildOnStartup', true)) {
        await buildTools(context);
    }
    if (!fs.existsSync(server)) {
        throw new Error(`language server not found at ${server}`);
    }
    diagnostics = vscode.languages.createDiagnosticCollection(LANGUAGE_ID);
    client = new RetroClient({
        command: server,
        args: ['--stdio', '--compiler', compiler, '--debounce',
            String(vscode.workspace.getConfiguration(LANGUAGE_ID).get('debounce', 200))],
        cwd: root,
        trace: vscode.workspace.getConfiguration(LANGUAGE_ID).get('trace', 'off') === 'messages',
        onDiagnostics: pushDiagnostics,
        onLog: (line) => log(line),
        onExit: (info) => log(`language server exited: code=${info.code} signal=${info.signal}`)
    });
    client.start();
    const result = await initialize(client, root);
    client.notify('initialized', {});
    log(`server ready: ${JSON.stringify(result && result.serverInfo)}`);
}

async function stopServer() {
    if (client === undefined) {
        return;
    }
    const stopping = client;
    client = undefined;
    if (diagnostics !== undefined) {
        diagnostics.dispose();
        diagnostics = undefined;
    }
    await stopping.stop();
}

async function restartServer(context) {
    await stopServer();
    await startServer(context);
}

function languageClient() {
    if (client === undefined) {
        return undefined;
    }
    return client;
}

function uriParams(document) {
    return { textDocument: { uri: document.uri.toString() } };
}

function positionParams(document, position) {
    return {
        textDocument: { uri: document.uri.toString() },
        position: { line: position.line, character: position.character }
    };
}

function registerProviders() {
    disposables.push(vscode.languages.registerHoverProvider({ language: LANGUAGE_ID }, {
        async provideHover(document, position) {
            const active = languageClient();
            if (active === undefined) {
                return null;
            }
            const result = await active.request('textDocument/hover', positionParams(document, position));
            const text = convert.hoverText(result);
            if (text === null) {
                return null;
            }
            return new vscode.Hover(new vscode.MarkdownString(text));
        }
    }));

    disposables.push(vscode.languages.registerDocumentSymbolProvider({ language: LANGUAGE_ID }, {
        async provideDocumentSymbols(document) {
            const active = languageClient();
            if (active === undefined) {
                return [];
            }
            const items = await active.request('textDocument/documentSymbol', uriParams(document));
            if (!Array.isArray(items)) {
                return [];
            }
            const toSymbol = (item) => {
                const range = convert.rangeOf(item);
                const selection = convert.selectionRangeOf(item);
                const symbol = new vscode.SymbolInformation(
                    item.name,
                    convert.symbolKind(item.kind),
                    new vscode.Range(range.startLine, range.startCharacter, range.endLine,
                        range.endCharacter),
                    new vscode.Range(selection.startLine, selection.startCharacter,
                        selection.endLine, selection.endCharacter));
                if (Array.isArray(item.children) && item.children.length > 0) {
                    symbol.children = item.children.map(toSymbol);
                }
                return symbol;
            };
            return items.map(toSymbol);
        }
    }));

    disposables.push(vscode.languages.registerCompletionItemProvider({ language: LANGUAGE_ID }, {
        triggerCharacters: ['(', '[', '.'],
        async provideCompletionItems(document, position) {
            const active = languageClient();
            if (active === undefined) {
                return [];
            }
            const result = await active.request('textDocument/completion',
                positionParams(document, position));
            const items = Array.isArray(result) ? result : (result || []).items || [];
            return items.map((item) => {
                const completion = new vscode.CompletionItem(item.label,
                    convert.completionItemKind(item.kind));
                if (typeof item.detail === 'string') {
                    completion.detail = item.detail;
                }
                if (typeof item.sortText === 'string') {
                    completion.sortText = item.sortText;
                }
                return completion;
            });
        }
    }));

    disposables.push(vscode.languages.registerSignatureHelpProvider({ language: LANGUAGE_ID }, {
        signatureHelpTriggerCharacters: ['(', ','],
        signatureHelpRetriggerCharacters: [')'],
        async provideSignatureHelp(document, position) {
            const active = languageClient();
            if (active === undefined) {
                return null;
            }
            const result = await active.request('textDocument/signatureHelp',
                positionParams(document, position));
            if (result === null || result === undefined || !Array.isArray(result.signatures)) {
                return null;
            }
            const help = new vscode.SignatureHelp();
            help.signatures = result.signatures.map((signature) => {
                const information = new vscode.SignatureInformation(
                    signature.label,
                    undefined,
                    ...(signature.parameters || []).map((parameter) => new vscode.ParameterInformation(
                        parameter.label)));
                return information;
            });
            const activeIndex = result.activeParameter || 0;
            help.activeSignature = 0;
            help.activeParameter = activeIndex < help.signatures[0].parameters.length
                ? activeIndex
                : 0;
            return help;
        }
    }));
}

function registerDocumentSync() {
    disposables.push(vscode.workspace.onDidOpenTextDocument((document) => {
        const active = languageClient();
        if (active === undefined || document.languageId !== LANGUAGE_ID) {
            return;
        }
        active.notify('textDocument/didOpen', {
            textDocument: {
                uri: document.uri.toString(),
                languageId: document.languageId,
                version: document.version,
                text: document.getText()
            }
        });
    }));
    disposables.push(vscode.workspace.onDidChangeTextDocument((event) => {
        const active = languageClient();
        if (active === undefined || event.document.languageId !== LANGUAGE_ID) {
            return;
        }
        active.notify('textDocument/didChange', {
            textDocument: { uri: event.document.uri.toString(), version: event.document.version },
            contentChanges: [{ text: event.document.getText() }]
        });
    }));
    disposables.push(vscode.workspace.onDidCloseTextDocument((document) => {
        const active = languageClient();
        if (active === undefined || document.languageId !== LANGUAGE_ID) {
            return;
        }
        active.notify('textDocument/didClose', {
            textDocument: { uri: document.uri.toString() }
        });
    }));
}

function registerSemanticTokens() {
    disposables.push(vscode.languages.registerDocumentSemanticTokensProvider({ language: LANGUAGE_ID },
        {
            async provideDocumentSemanticTokens(document) {
                const active = languageClient();
                if (active === undefined) {
                    return { data: [] };
                }
                const result = await active.request('textDocument/semanticTokens/full',
                    uriParams(document));
                const data = (result || {}).data || [];
                return { data };
            }
        },
        new vscode.DocumentSemanticTokensLegend(convert.TOKEN_TYPES, convert.TOKEN_MODIFIERS)));
}

function activate(context) {
    output = vscode.window.createOutputChannel('Retro C');
    disposables.push(output);
    context.subscriptions.push(...disposables);
    registerDocumentSync();
    registerProviders();
    registerSemanticTokens();
    context.subscriptions.push(vscode.commands.registerCommand('retro-c.restartServer',
        async () => {
            try {
                await restartServer(context);
            } catch (error) {
                log(`restart failed: ${error.message}`);
                vscode.window.showErrorMessage(`Retro C: ${error.message}`);
            }
        }));
    context.subscriptions.push(vscode.commands.registerCommand('retro-c.buildTools',
        async () => {
            try {
                await buildTools(context);
                vscode.window.showInformationMessage('Retro C: build finished');
            } catch (error) {
                vscode.window.showErrorMessage(`Retro C: ${error.message}`);
            }
        }));
    context.subscriptions.push(vscode.commands.registerCommand('retro-c.showOutput', () => {
        output.show(true);
    }));
    for (const tool of missingTools(context)) {
        log(`missing ${path.relative(repositoryRoot(context), tool.output)}`);
    }
    return startServer(context).then(
        () => {
            for (const document of vscode.workspace.textDocuments) {
                if (document.languageId === LANGUAGE_ID) {
                    client.notify('textDocument/didOpen', {
                        textDocument: {
                            uri: document.uri.toString(),
                            languageId: document.languageId,
                            version: document.version,
                            text: document.getText()
                        }
                    });
                }
            }
        },
        (error) => {
            log(`unable to start: ${error.message}`);
            vscode.window.showErrorMessage(`Retro C: ${error.message}`);
        });
}

function deactivate() {
    return stopServer();
}

module.exports = { activate, deactivate, toolchain, missingTools, buildTools, repositoryRoot };
