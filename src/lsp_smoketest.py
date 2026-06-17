#!/usr/bin/env python3
"""Tiny LSP client to smoke-test bin/classyc-lsp.

Sends initialize / didOpen / didChange / shutdown / exit and prints every
server->client message (notably textDocument/publishDiagnostics)."""

import json
import subprocess
import sys
import threading

LSP = sys.argv[1] if len(sys.argv) > 1 else "./bin/classyc-lsp"

proc = subprocess.Popen([LSP], stdin=subprocess.PIPE, stdout=subprocess.PIPE)


def send(obj):
    body = json.dumps(obj).encode()
    proc.stdin.write(b"Content-Length: %d\r\n\r\n" % len(body) + body)
    proc.stdin.flush()


def reader():
    f = proc.stdout
    while True:
        # read headers
        length = None
        while True:
            line = f.readline()
            if not line:
                return
            line = line.strip()
            if line == b"":
                break
            if line.lower().startswith(b"content-length:"):
                length = int(line.split(b":")[1])
        body = f.read(length)
        msg = json.loads(body)
        if msg.get("method") == "textDocument/publishDiagnostics":
            p = msg["params"]
            print(f"\n== diagnostics for {p['uri']} ==")
            if not p["diagnostics"]:
                print("  (none)")
            for d in p["diagnostics"]:
                s = d["range"]["start"]
                sev = "error" if d["severity"] == 1 else "warn"
                print(f"  [{sev}] {s['line'] + 1}:{s['character'] + 1}  {d['message']}")
        elif "result" in msg:
            print(f"<- response id={msg.get('id')}: {json.dumps(msg['result'])[:120]}")


t = threading.Thread(target=reader, daemon=True)
t.start()

VALID = 'class Point {\n    int x, y;\n    Point(int x, int y) { this.x = x; this.y = y; }\n    int sum() { return this.x + this.y; }\n};\nint main() {\n    Point* p = new Point(3, 4);\n    String s = "hi " + "there";\n    return p->sum();\n}\n'

BAD = 'int main() {\n    String s = "x";\n    int y = s - 3;\n    undeclared_thing();\n    return 0\n}\n'

send(
    {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"capabilities": {}}}
)
send({"jsonrpc": "2.0", "method": "initialized", "params": {}})
send(
    {
        "jsonrpc": "2.0",
        "method": "textDocument/didOpen",
        "params": {
            "textDocument": {
                "uri": "file:///tmp/demo.cy",
                "languageId": "classyc",
                "version": 1,
                "text": VALID,
            }
        },
    }
)
send(
    {
        "jsonrpc": "2.0",
        "method": "textDocument/didChange",
        "params": {
            "textDocument": {"uri": "file:///tmp/demo.cy", "version": 2},
            "contentChanges": [{"text": BAD}],
        },
    }
)
send({"jsonrpc": "2.0", "id": 2, "method": "shutdown"})
send({"jsonrpc": "2.0", "method": "exit"})

proc.wait(timeout=10)
import time

time.sleep(0.2)
