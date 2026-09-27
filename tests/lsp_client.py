#!/usr/bin/env python3
"""Drive tools/retrolsp over stdio and print a deterministic key=value report.

Usage: lsp_client.py --server PATH --compiler PATH [--root DIR] [--debounce MS]
                     [--garbage HEXBYTES] SCENARIO.json

The scenario is a JSON array of steps. Each step is one of:

    {"method": "initialize"}
    {"notification": "textDocument/didOpen", "file": "relative/path.rc",
     "text": "inline source", "uri": "optional-uri"}
    {"method": "textDocument/documentSymbol"}
    {"method": "textDocument/hover", "line": 3, "character": 5}
    {"raw": "hex encoded bytes sent before anything else"}

Every request result is flattened into `key=value` lines using dotted keys and
bracketed indices, for example `documentSymbol[0].name=minimum`. Diagnostics
notifications are reported as `diagnostic[i].<field>`. The exit status of the
server is printed as `exitCode=N`.
"""

import argparse
import binascii
import json
import subprocess
import sys


def frame(payload):
    body = json.dumps(payload).encode("utf-8")
    return b"Content-Length: %d\r\n\r\n" % len(body) + body


class Client:
    def __init__(self, server, compiler, debounce):
        self.process = subprocess.Popen(
            [server, "--stdio", "--compiler", compiler, "--debounce", str(debounce)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        self.next_id = 1
        self.diagnostics = []
        self.lines = []

    def send_raw(self, data):
        self.process.stdin.write(data)
        self.process.stdin.flush()

    def send(self, payload):
        self.send_raw(frame(payload))

    def read(self):
        length = None
        while True:
            line = self.process.stdout.readline()
            if not line:
                raise EOFError("server closed the stream")
            line = line.strip()
            if not line:
                break
            if line.lower().startswith(b"content-length:"):
                length = int(line.split(b":")[1])
        if length is None:
            raise ValueError("missing Content-Length header")
        return json.loads(self.process.stdout.read(length))

    def request(self, method, params):
        ident = self.next_id
        self.next_id += 1
        self.send({"jsonrpc": "2.0", "id": ident, "method": method, "params": params})
        while True:
            message = self.read()
            if "id" not in message:
                self.note(message)
                continue
            if message["id"] != ident:
                raise ValueError("out of order response %r" % message["id"])
            return message

    def notify(self, method, params):
        self.send({"jsonrpc": "2.0", "method": method, "params": params})

    def semantic_tokens(self, key, data):
        line = 0
        character = 0
        self.lines.append("%s.count=%d" % (key, len(data) // 5))
        for index in range(0, len(data) - 4, 5):
            delta_line, delta_start, length, kind, _ = data[index:index + 5]
            line += delta_line
            character = delta_start if delta_line else character + delta_start
            self.lines.append("%s[%d].line=%d" % (key, index // 5, line))
            self.lines.append("%s[%d].character=%d" % (key, index // 5, character))
            self.lines.append("%s[%d].length=%d" % (key, index // 5, length))
            self.lines.append("%s[%d].type=%d" % (key, index // 5, kind))

    def note(self, message):
        if message.get("method") == "textDocument/publishDiagnostics":
            params = message.get("params", {})
            index = len(self.diagnostics)
            self.diagnostics.append(params)
            self.lines.append("diagnostic[%d].uri=%s" % (index, params.get("uri", "")))
            items = params.get("diagnostics", [])
            self.lines.append("diagnostic[%d].count=%d" % (index, len(items)))
            for position, item in enumerate(items):
                prefix = "diagnostic[%d].item[%d]" % (index, position)
                rng = item.get("range", {})
                start = rng.get("start", {})
                end = rng.get("end", {})
                self.lines.append("%s.severity=%s" % (prefix, item.get("severity")))
                self.lines.append("%s.source=%s" % (prefix, item.get("source")))
                self.lines.append("%s.message=%s" % (prefix, item.get("message")))
                self.lines.append("%s.start.line=%s" % (prefix, start.get("line")))
                self.lines.append("%s.start.character=%s" % (prefix, start.get("character")))
                self.lines.append("%s.end.line=%s" % (prefix, end.get("line")))
                self.lines.append("%s.end.character=%s" % (prefix, end.get("character")))

    def flatten(self, prefix, value):
        if isinstance(value, dict):
            for key, item in value.items():
                self.flatten("%s.%s" % (prefix, key), item)
        elif isinstance(value, list):
            for index, item in enumerate(value):
                self.flatten("%s[%d]" % (prefix, index), item)
        elif isinstance(value, bool):
            self.lines.append("%s=%s" % (prefix, "true" if value else "false"))
        else:
            self.lines.append("%s=%s" % (prefix, value))

    def close(self, expect_exit_zero=True):
        self.request("shutdown", {})
        self.notify("exit", {})
        self.process.stdin.close()
        code = self.process.wait(timeout=30)
        self.lines.append("exitCode=%d" % code)
        if expect_exit_zero and code != 0:
            raise ValueError("expected exit 0, got %d" % code)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", required=True)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--root", default=".")
    parser.add_argument("--debounce", type=int, default=20)
    parser.add_argument("--garbage", default="")
    parser.add_argument("scenario")
    arguments = parser.parse_args()

    with open(arguments.scenario, "r", encoding="utf-8") as handle:
        steps = json.load(handle)

    client = Client(arguments.server, arguments.compiler, arguments.debounce)
    label = "step"
    if arguments.garbage:
        client.send_raw(binascii.unhexlify(arguments.garbage))
    for index, step in enumerate(steps):
        label = "step %d (%s)" % (index, step.get("method") or step.get("notification")
                                 or "raw")
        if "raw" in step:
            client.send_raw(binascii.unhexlify(step["raw"]))
            continue
        if "method" in step and step["method"] == "initialize":
            response = client.request("initialize", {"processId": None, "rootUri": None,
                                                      "capabilities": {}})
            client.flatten("initialize", response.get("result", {}))
            client.notify("initialized", {})
            continue
        if "notification" in step:
            uri = step.get("uri")
            if "file" in step:
                with open("%s/%s" % (arguments.root, step["file"]), "r",
                          encoding="utf-8") as handle:
                    text = handle.read()
                uri = "file:///" + step["file"]
            else:
                text = step.get("text", "")
            version = step.get("version", 1)
            if step["notification"] == "textDocument/didChange":
                params = {"textDocument": {"uri": uri, "version": version},
                          "contentChanges": [{"text": text}]}
            elif step["notification"] == "textDocument/didClose":
                params = {"textDocument": {"uri": uri}}
            else:
                params = {"textDocument": {"uri": uri, "languageId": "retro-c",
                                           "version": version, "text": text}}
            client.notify(step["notification"], params)
            continue
        params = {}
        if "file" in step:
            params["textDocument"] = {"uri": "file:///" + step["file"]}
        elif "uri" in step:
            params["textDocument"] = {"uri": step["uri"]}
        if "line" in step:
            params["position"] = {"line": step["line"], "character": step["character"]}
        response = client.request(step["method"], params)
        key = step.get("key", step["method"].rsplit("/", 1)[-1])
        if "error" in response:
            client.lines.append("%s.error.code=%s" % (key, response["error"].get("code")))
            client.lines.append("%s.error.message=%s" % (key, response["error"].get("message")))
        elif step.get("decode") == "semanticTokens":
            client.semantic_tokens(key, response.get("result", {}).get("data", []))
        else:
            client.flatten(key, response.get("result"))
    client.close()
    for line in client.lines:
        print(line)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:  # noqa: BLE001 - report driver failures verbatim
        print("driver-error: %s" % error, file=sys.stderr)
        sys.exit(1)
