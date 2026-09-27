'use strict';

const TOKEN_TYPES = [
    'type',
    'variable',
    'parameter',
    'function',
    'property',
    'keyword',
    'number',
    'comment',
    'operator',
    'punctuation',
    'macro',
    'invalid'
];

const TOKEN_MODIFIERS = [];

const DIAGNOSTIC_SEVERITY = { 1: 0, 2: 1, 3: 2, 4: 3 };

const COMPLETION_KIND = {
    1: 0,
    2: 1,
    3: 2,
    4: 2,
    5: 5,
    6: 5,
    7: 6,
    9: 9,
    10: 11,
    12: 11,
    13: 12,
    14: 13,
    17: 17,
    19: 19,
    20: 20,
    21: 20,
    22: 5
};

const SYMBOL_KIND = {
    1: 1,
    2: 4,
    5: 4,
    6: 5,
    10: 10,
    11: 10,
    12: 11,
    13: 12,
    14: 14,
    22: 22
};

function diagnosticSeverity(value) {
    const mapped = DIAGNOSTIC_SEVERITY[value];
    return mapped === undefined ? 0 : mapped;
}

function rangeOf(item) {
    const range = (item && item.range) || {};
    const start = range.start || {};
    const end = range.end || {};
    return {
        startLine: start.line || 0,
        startCharacter: start.character || 0,
        endLine: end.line === undefined ? start.line || 0 : end.line,
        endCharacter: end.character === undefined ? start.character || 0 : end.character
    };
}

function selectionRangeOf(item) {
    const range = (item && item.selectionRange) || null;
    if (range === null) {
        return rangeOf(item);
    }
    const start = range.start || {};
    const end = range.end || {};
    return {
        startLine: start.line || 0,
        startCharacter: start.character || 0,
        endLine: end.line === undefined ? start.line || 0 : end.line,
        endCharacter: end.character === undefined ? start.character || 0 : end.character
    };
}

function completionItemKind(value) {
    const mapped = COMPLETION_KIND[value];
    return mapped === undefined ? 5 : mapped;
}

function symbolKind(value) {
    const mapped = SYMBOL_KIND[value];
    return mapped === undefined ? 12 : mapped;
}

const WINDOWS_NOTICE = 'the Retro C language server currently supports Linux and macOS only; ' +
    'see docs/lsp.md';

function toolFailureMessage(platform, code, target) {
    if (code !== 'ENOENT') {
        return `${target}: ${code}`;
    }
    if (platform === 'win32') {
        return `${target} was not found. On Windows, ${WINDOWS_NOTICE}`;
    }
    return `${target} was not found`;
}

function positionOf(value) {
    const position = value || {};
    return { line: position.line || 0, character: position.character || 0 };
}

function hoverText(result) {
    if (result === null || result === undefined) {
        return null;
    }
    const contents = result.contents;
    if (typeof contents === 'string') {
        return contents;
    }
    if (contents !== null && typeof contents === 'object' && typeof contents.value === 'string') {
        return contents.value;
    }
    if (Array.isArray(contents)) {
        const parts = [];
        for (const entry of contents) {
            if (typeof entry === 'string') {
                parts.push(entry);
            } else if (entry !== null && typeof entry === 'object' && typeof entry.value === 'string') {
                parts.push(entry.value);
            }
        }
        return parts.length === 0 ? null : parts.join('\n\n');
    }
    return null;
}

function decodeSemanticTokens(data) {
    if (!Array.isArray(data)) {
        return [];
    }
    const tokens = [];
    let line = 0;
    let character = 0;
    for (let index = 0; index + 4 < data.length; index += 5) {
        const deltaLine = data[index];
        const deltaStart = data[index + 1];
        const length = data[index + 2];
        const type = data[index + 3];
        if (typeof deltaLine !== 'number' || typeof deltaStart !== 'number' ||
            typeof length !== 'number' || typeof type !== 'number') {
            break;
        }
        line += deltaLine;
        character = deltaLine === 0 ? character + deltaStart : deltaStart;
        tokens.push({ line, character, length, type });
    }
    return tokens;
}

module.exports = {
    TOKEN_TYPES,
    TOKEN_MODIFIERS,
    WINDOWS_NOTICE,
    diagnosticSeverity,
    rangeOf,
    selectionRangeOf,
    completionItemKind,
    symbolKind,
    positionOf,
    hoverText,
    toolFailureMessage,
    decodeSemanticTokens
};
