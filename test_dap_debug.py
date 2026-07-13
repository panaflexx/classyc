#!/usr/bin/env python3
"""
DAP breakpoint / continue smoke test for jitrunner --dap-stdio.

Flow:
  initialize → setBreakpoints → configurationDone → launch
  → expect stopped (breakpoint)
  → stackTrace (optional sanity)
  → continue
  → expect exited + terminated
  → disconnect
"""

import json
import queue
import re
import subprocess
import sys
import threading
import time


def encode_message(obj):
    body = json.dumps(obj, separators=(",", ":")).encode("utf-8")
    header = f"Content-Length: {len(body)}\r\n\r\n".encode("utf-8")
    return header + body


class DAPTestClient:
    def __init__(self, executable, bmir_path):
        self.client_seq = 1
        self.server_seqs = []
        self.received_messages = []
        self.msg_queue = queue.Queue()
        self.proc = subprocess.Popen(
            [executable, "--dap-stdio", bmir_path, "--mode", "interp"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            cwd="/home/rdavenpo/src/MIR/classyc",
            bufsize=0,
            env={
                **dict(**{k: v for k, v in __import__("os").environ.items()}),
                "CLASSYC_DAP_LOG": "dapdebug-bp.log",
            },
        )
        self.reader_thread = threading.Thread(target=self._reader_loop, daemon=True)
        self.reader_thread.start()

    def _reader_loop(self):
        stdout = self.proc.stdout
        while True:
            try:
                msg = self._read_one_message(stdout)
                if msg is None:
                    break
                self.received_messages.append(msg)
                if "seq" in msg:
                    self.server_seqs.append(msg["seq"])
                self.msg_queue.put(msg)
            except Exception as e:
                print(f"[READER] error: {e}", file=sys.stderr)
                break

    def _read_one_message(self, stream):
        header = b""
        while b"\r\n\r\n" not in header:
            chunk = stream.read(1)
            if not chunk:
                return None
            header += chunk
        m = re.search(rb"[Cc]ontent-[Ll]ength:\s*(\d+)", header)
        if not m:
            return None
        length = int(m.group(1))
        body = stream.read(length)
        if len(body) < length:
            return None
        return json.loads(body.decode("utf-8", errors="replace"))

    def send_request(self, command, arguments=None):
        msg = {"type": "request", "seq": self.client_seq, "command": command}
        if arguments is not None:
            msg["arguments"] = arguments
        self.proc.stdin.write(encode_message(msg))
        self.proc.stdin.flush()
        sent = self.client_seq
        self.client_seq += 1
        return sent

    def wait_for_response(self, expected_request_seq, timeout=15.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                msg = self.msg_queue.get(timeout=0.2)
                if (
                    msg.get("type") == "response"
                    and msg.get("request_seq") == expected_request_seq
                ):
                    return msg
            except queue.Empty:
                continue
        return None

    def wait_for_event(self, event_name, timeout=15.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                msg = self.msg_queue.get(timeout=0.2)
                if msg.get("type") == "event" and msg.get("event") == event_name:
                    return msg
            except queue.Empty:
                continue
        return None

    def shutdown(self):
        try:
            self.proc.stdin.close()
        except Exception:
            pass
        try:
            self.proc.terminate()
            self.proc.wait(timeout=2)
        except Exception:
            try:
                self.proc.kill()
            except Exception:
                pass


def main():
    print("=== DAP breakpoint debug test ===")
    # Recompile sample with debug info each run
    compile = subprocess.run(
        [
            "./bin/classyc",
            "-I",
            "include",
            "-c",
            "-g",
            "-o",
            "examples/debug-sample.bmir",
            "examples/debug-sample.cy",
        ],
        cwd="/home/rdavenpo/src/MIR/classyc",
        capture_output=True,
        text=True,
    )
    if compile.returncode != 0:
        print("FAIL: compile failed")
        print(compile.stdout)
        print(compile.stderr)
        return 1

    client = DAPTestClient("./bin/jitrunner", "examples/debug-sample.bmir")
    try:
        # 1. initialize
        s = client.send_request(
            "initialize",
            {
                "clientID": "python-dap-debug-test",
                "adapterID": "jitrunner",
                "linesStartAt1": True,
                "columnsStartAt1": True,
                "pathFormat": "path",
            },
        )
        resp = client.wait_for_response(s)
        assert resp and resp.get("success"), f"initialize failed: {resp}"
        print(f"<- initialize ok seq={resp['seq']}")

        ev = client.wait_for_event("initialized", timeout=5.0)
        assert ev, "no initialized event"
        print("<- initialized event")

        # 2. setBreakpoints on a line that survives MIR link inlining.
        #    `add`'s return (line 8) is optimized away when add is inlined into
        #    main, so break on the computation line instead (line 7).
        bp_line = 7
        s = client.send_request(
            "setBreakpoints",
            {
                "source": {
                    "path": "examples/debug-sample.cy",
                    "name": "debug-sample.cy",
                },
                "breakpoints": [{"line": bp_line}],
                "sourceModified": False,
            },
        )
        resp = client.wait_for_response(s)
        assert resp and resp.get("success"), f"setBreakpoints failed: {resp}"
        bps = (resp.get("body") or {}).get("breakpoints") or []
        print(f"<- setBreakpoints: {bps}")
        assert bps, "expected at least one verified breakpoint"

        # 3. configurationDone
        s = client.send_request("configurationDone")
        resp = client.wait_for_response(s)
        assert resp and resp.get("success"), f"configurationDone failed: {resp}"
        print("<- configurationDone ok")

        # 4. launch
        s = client.send_request(
            "launch", {"program": "examples/debug-sample.bmir", "noDebug": False}
        )
        resp = client.wait_for_response(s)
        assert resp and resp.get("success"), f"launch failed: {resp}"
        print("<- launch ok; waiting for stopped...")

        # 5. expect stopped (or fall through if no line maps — still fail clearly)
        stopped = None
        deadline = time.time() + 20.0
        while time.time() < deadline and stopped is None:
            try:
                msg = client.msg_queue.get(timeout=0.3)
                if msg.get("type") == "event" and msg.get("event") == "stopped":
                    stopped = msg
                elif msg.get("type") == "event" and msg.get("event") == "terminated":
                    print("FAIL: terminated before stopped — breakpoint never hit")
                    print("  last messages:", client.received_messages[-8:])
                    return 1
            except queue.Empty:
                continue

        assert stopped, "never received stopped event"
        print(f"<- stopped: {stopped.get('body')}")

        # 6. stackTrace should report the BP line
        s = client.send_request(
            "stackTrace", {"threadId": 1, "startFrame": 0, "levels": 20}
        )
        resp = client.wait_for_response(s, timeout=5.0)
        assert resp and resp.get("success"), f"stackTrace failed: {resp}"
        frames = (resp.get("body") or {}).get("stackFrames") or []
        print(f"<- stackTrace frames={frames}")
        if frames:
            line = frames[0].get("line")
            # Accept the exact BP line (or nearby if maps are coarse)
            assert bp_line - 1 <= int(line) <= bp_line + 1, (
                f"unexpected stop line {line}, wanted ~{bp_line}"
            )

        # 7. continue
        s = client.send_request("continue", {"threadId": 1})
        resp = client.wait_for_response(s, timeout=5.0)
        assert resp and resp.get("success"), f"continue failed: {resp}"
        print("<- continue ok; waiting for terminated...")

        term = None
        deadline = time.time() + 15.0
        while time.time() < deadline and term is None:
            try:
                msg = client.msg_queue.get(timeout=0.3)
                if msg.get("type") == "event" and msg.get("event") == "terminated":
                    term = msg
            except queue.Empty:
                continue
        assert term, "never received terminated after continue"
        print("<- terminated")

        # 8. disconnect
        s = client.send_request(
            "disconnect", {"restart": False, "terminateDebuggee": True}
        )
        resp = client.wait_for_response(s, timeout=3.0)
        if resp:
            print(f"<- disconnect success={resp.get('success')}")

        # Strict seq check
        expected = list(range(1, len(client.server_seqs) + 1))
        if client.server_seqs != expected:
            print(f"FAIL: non-consecutive server seqs: {client.server_seqs}")
            return 1

        print("\n=== ALL DEBUG CHECKS PASSED ===")
        return 0

    except AssertionError as e:
        print(f"\nFAIL: {e}")
        print("Last 12 messages:")
        for m in client.received_messages[-12:]:
            print("  ", m)
        return 1
    except Exception as e:
        print(f"\nFAIL: unexpected: {e}")
        import traceback

        traceback.print_exc()
        return 1
    finally:
        client.shutdown()
        try:
            err = client.proc.stderr.read().decode("utf-8", errors="replace")
            if err.strip():
                print("\n--- jitrunner stderr ---")
                print(err.strip()[-4000:])
        except Exception:
            pass
        try:
            with open("dapdebug-bp.log", "r", errors="replace") as f:
                log = f.read()
            if log.strip():
                print("\n--- dapdebug-bp.log (tail) ---")
                print(log[-3000:])
        except Exception:
            pass


if __name__ == "__main__":
    sys.exit(main())
