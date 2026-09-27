'use strict';

const { spawn } = require('child_process');

const HEADER_LIMIT = 8192;

function encode(message) {
    const body = Buffer.from(JSON.stringify(message), 'utf8');
    const head = Buffer.from(`Content-Length: ${body.length}\r\n\r\n`, 'ascii');
    return Buffer.concat([head, body]);
}

class Transport {
    constructor(onMessage, onLog) {
        this.onMessage = onMessage;
        this.onLog = onLog;
        this.buffer = Buffer.alloc(0);
    }

    append(chunk) {
        this.buffer = this.buffer.length === 0 ? chunk : Buffer.concat([this.buffer, chunk]);
    }

    next() {
        for (;;) {
            const separator = this.buffer.indexOf('\r\n\r\n');
            if (separator < 0) {
                if (this.buffer.length > HEADER_LIMIT) {
                    throw new Error('language server sent an oversized header block');
                }
                return null;
            }
            const header = this.buffer.subarray(0, separator).toString('ascii');
            let length = -1;
            for (const line of header.split('\r\n')) {
                const colon = line.indexOf(':');
                if (colon < 0) {
                    continue;
                }
                if (line.slice(0, colon).trim().toLowerCase() !== 'content-length') {
                    continue;
                }
                const value = line.slice(colon + 1).trim();
                if (!/^[0-9]+$/.test(value)) {
                    continue;
                }
                length = Number.parseInt(value, 10);
            }
            if (length < 0) {
                this.onLog(`ignoring message without Content-Length: ${JSON.stringify(header)}`);
                this.buffer = this.buffer.subarray(separator + 4);
                continue;
            }
            if (this.buffer.length < separator + 4 + length) {
                return null;
            }
            const body = this.buffer.subarray(separator + 4, separator + 4 + length).toString('utf8');
            this.buffer = this.buffer.subarray(separator + 4 + length);
            try {
                this.onMessage(JSON.parse(body));
            } catch (error) {
                this.onLog(`ignoring unparsable message body: ${error.message}`);
            }
        }
    }
}

class RetroClient {
    constructor(options) {
        this.command = options.command;
        this.args = options.args || [];
        this.cwd = options.cwd;
        this.trace = Boolean(options.trace);
        this.onDiagnostics = options.onDiagnostics || (() => {});
        this.onLog = options.onLog || (() => {});
        this.onExit = options.onExit || (() => {});
        this.process = null;
        this.pending = new Map();
        this.nextId = 1;
        this.exitInfo = null;
        this.transport = new Transport(
            (message) => this.dispatch(message),
            (line) => this.onLog(line));
    }

    start() {
        if (this.process !== null) {
            return;
        }
        this.exitInfo = null;
        this.process = spawn(this.command, this.args, {
            cwd: this.cwd,
            stdio: ['pipe', 'pipe', 'pipe']
        });
        this.process.stdout.on('data', (chunk) => {
            this.transport.append(chunk);
            try {
                this.transport.next();
            } catch (error) {
                this.onLog(`protocol error: ${error.message}`);
            }
        });
        this.process.stderr.on('data', (chunk) => {
            this.onLog(`server: ${chunk.toString('utf8').trimEnd()}`);
        });
        this.process.on('error', (error) => {
            this.onLog(`unable to start ${this.command}: ${error.message}`);
            this.failAll(new Error(`unable to start ${this.command}`));
        });
        this.process.on('exit', (code, signal) => {
            this.exitInfo = { code, signal };
            this.failAll(new Error('language server exited'));
            this.process = null;
            this.onExit(this.exitInfo);
        });
    }

    failAll(error) {
        for (const [, entry] of this.pending) {
            entry.reject(error);
        }
        this.pending.clear();
    }

    stop() {
        if (this.process === null) {
            return Promise.resolve();
        }
        const child = this.process;
        const done = this.request('shutdown', {}).catch(() => undefined);
        return done.then(() => {
            this.notify('exit', {});
            return new Promise((resolve) => {
                if (child.exitCode !== null || child.signalCode !== null) {
                    resolve();
                    return;
                }
                child.once('exit', () => resolve());
                setTimeout(() => {
                    child.kill('SIGKILL');
                    resolve();
                }, 2000);
            });
        });
    }

    write(message) {
        if (this.process === null || this.process.stdin === null) {
            return;
        }
        const payload = encode(message);
        if (this.trace) {
            this.onLog(`-> ${payload.subarray(payload.indexOf('\r\n\r\n') + 4).toString('utf8')}`);
        }
        this.process.stdin.write(payload);
    }

    notify(method, params) {
        this.write({ jsonrpc: '2.0', method, params });
    }

    request(method, params) {
        if (this.process === null) {
            return Promise.reject(new Error('language server is not running'));
        }
        const id = this.nextId;
        this.nextId += 1;
        return new Promise((resolve, reject) => {
            this.pending.set(id, { resolve, reject });
            this.write({ jsonrpc: '2.0', id, method, params });
        });
    }

    dispatch(message) {
        if (this.trace && message && message.id !== undefined) {
            this.onLog(`<- ${JSON.stringify(message)}`);
        }
        if (message && message.id !== undefined && message.id !== null) {
            const entry = this.pending.get(message.id);
            if (entry === undefined) {
                this.onLog(`ignoring response for unknown id ${message.id}`);
                return;
            }
            this.pending.delete(message.id);
            if (message.error !== undefined) {
                entry.reject(new Error(`${message.error.message} (${message.error.code})`));
                return;
            }
            entry.resolve(message.result);
            return;
        }
        if (message === null || message === undefined || message.method === undefined) {
            this.onLog(`ignoring unexpected message ${JSON.stringify(message)}`);
            return;
        }
        if (message.method === 'textDocument/publishDiagnostics') {
            this.onDiagnostics(message.params);
            return;
        }
        this.onLog(`ignoring notification ${message.method}`);
    }
}

function initialize(client, rootPath) {
    return client.request('initialize', {
        processId: process.pid,
        rootUri: rootPath === undefined ? null : `file://${rootPath}`,
        rootPath: rootPath === undefined ? null : rootPath,
        capabilities: {
            textDocument: {
                synchronization: { dynamicRegistration: false },
                publishDiagnostics: { relatedInformation: false },
                hover: { contentFormat: ['markdown', 'plaintext'] },
                documentSymbol: { hierarchicalDocumentSymbolSupport: true },
                completion: { completionItem: { snippetSupport: false } },
                signatureHelp: { signatureInformation: { documentationFormat: ['markdown'] } }
            },
            workspace: { workspaceFolders: false }
        },
        trace: 'off'
    });
}

module.exports = { RetroClient, Transport, encode, initialize };
