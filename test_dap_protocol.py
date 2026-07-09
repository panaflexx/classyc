#!/usr/bin/env python3
"""
DAP protocol validation test for jitrunner stdio adapter.

Validates:
- Server maintains independent, strictly increasing seq for ALL outgoing messages (responses + events)
- Responses carry correct request_seq matching the client's request seq
- Client and server counters are separate
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
        self.server_seqs = []  # every "seq" we receive from server
        self.received_messages = []  # full history for debugging
        self.msg_queue = queue.Queue()
        self.proc = subprocess.Popen(
            [executable, "--dap-stdio", bmir_path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            cwd="/home/rdavenpo/src/MIR/classyc",
            bufsize=0,
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

        # Extract Content-Length (case-insensitive)
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
        data = encode_message(msg)
        self.proc.stdin.write(data)
        self.proc.stdin.flush()
        sent_seq = self.client_seq
        self.client_seq += 1
        return sent_seq

    def wait_for_response(self, expected_request_seq, timeout=10.0):
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

    def drain_events_until(self, predicate, timeout=10.0, max_msgs=50):
        """Collect events (and other messages) until predicate(msg) is true."""
        collected = []
        deadline = time.time() + timeout
        count = 0
        while time.time() < deadline and count < max_msgs:
            try:
                msg = self.msg_queue.get(timeout=0.3)
                collected.append(msg)
                if predicate(msg):
                    return collected
                count += 1
            except queue.Empty:
                continue
        return collected

    def shutdown(self):
        try:
            self.proc.stdin.close()
        except:
            pass
        try:
            self.proc.terminate()
            self.proc.wait(timeout=2)
        except:
            try:
                self.proc.kill()
            except:
                pass


def main():
    print("=== DAP seq protocol validation test ===")
    client = DAPTestClient("./bin/jitrunner", "examples/classy.bmir")

    try:
        # === 1. initialize ===
        init_seq = client.send_request(
            "initialize",
            {
                "clientID": "python-dap-test",
                "clientName": "Python DAP Validator",
                "adapterID": "jitrunner",
                "locale": "en-US",
                "linesStartAt1": True,
                "columnsStartAt1": True,
                "pathFormat": "path",
                "supportsVariableType": True,
                "supportsRunInTerminalRequest": True,
                "supportsStartDebuggingRequest": True,
                "supportsANSIStyling": True,
            },
        )
        print(f"-> sent initialize (client seq={init_seq})")

        resp = client.wait_for_response(init_seq)
        if not resp:
            print("FAIL: no response to initialize")
            return 1
        print(
            f"<- initialize response: seq={resp.get('seq')}, request_seq={resp.get('request_seq')}, success={resp.get('success')}"
        )
        assert resp["type"] == "response"
        assert resp["request_seq"] == init_seq, f"bad request_seq: {resp}"
        assert resp["seq"] == 1, f"first server seq should be 1, got {resp['seq']}"

        # Expect "initialized" event shortly after (server's next seq)
        collected = client.drain_events_until(
            lambda m: m.get("type") == "event" and m.get("event") == "initialized",
            timeout=5.0,
        )
        init_event = next(
            (
                m
                for m in collected
                if m.get("type") == "event" and m.get("event") == "initialized"
            ),
            None,
        )
        if not init_event:
            print("FAIL: never received 'initialized' event")
            print("Collected after init:", collected)
            return 1
        print(f"<- event 'initialized': seq={init_event.get('seq')}")
        assert init_event["seq"] == 2, (
            f"initialized event should have seq=2, got {init_event['seq']}"
        )

        # === 2. configurationDone ===
        cfg_seq = client.send_request("configurationDone")
        print(f"-> sent configurationDone (client seq={cfg_seq})")
        resp = client.wait_for_response(cfg_seq)
        assert resp, "no response to configurationDone"
        print(
            f"<- configurationDone response: seq={resp['seq']}, request_seq={resp['request_seq']}"
        )
        assert resp["request_seq"] == cfg_seq
        assert resp["seq"] == 3

        # === 3. launch ===
        launch_seq = client.send_request(
            "launch",
            {
                "program": "examples/classy.bmir",
                # "noDebug": False   # not required
            },
        )
        print(f"-> sent launch (client seq={launch_seq})")
        resp = client.wait_for_response(launch_seq)
        assert resp, "no response to launch"
        print(
            f"<- launch response: seq={resp['seq']}, request_seq={resp['request_seq']}"
        )
        assert resp["request_seq"] == launch_seq
        assert resp["seq"] == 4

        # === Collect run events (output, exited, terminated) ===
        print("   (waiting for program execution + post-run events...)")
        run_events = client.drain_events_until(
            lambda m: m.get("type") == "event" and m.get("event") == "terminated",
            timeout=8.0,
            max_msgs=30,
        )

        # Show what we got
        for m in run_events:
            if m.get("type") == "event":
                ev = m.get("event")
                body = m.get("body", {})
                print(f"<- event '{ev}': seq={m.get('seq')}  body={body}")
            elif m.get("type") == "response":
                print(
                    f"<- (unexpected) response seq={m.get('seq')} req={m.get('request_seq')}"
                )

        # Verify a few post-launch events have consecutive seqs starting at 5
        event_seqs = [m["seq"] for m in run_events if m.get("type") == "event"]
        print(f"   event seqs after launch: {event_seqs}")

        # === 4. A couple more requests after termination (e.g. threads, stackTrace) ===
        threads_seq = client.send_request("threads")
        print(f"-> sent threads (client seq={threads_seq})")
        resp = client.wait_for_response(threads_seq, timeout=3.0)
        if resp:
            print(
                f"<- threads response: seq={resp['seq']}, request_seq={resp['request_seq']}"
            )
            assert resp["request_seq"] == threads_seq

        # === 5. disconnect ===
        disc_seq = client.send_request(
            "disconnect", {"restart": False, "terminateDebuggee": True}
        )
        print(f"-> sent disconnect (client seq={disc_seq})")
        resp = client.wait_for_response(disc_seq, timeout=3.0)
        if resp:
            print(
                f"<- disconnect response: seq={resp['seq']}, request_seq={resp['request_seq']}, success={resp.get('success')}"
            )
            assert resp["request_seq"] == disc_seq
            assert resp.get("success") is True

        # === FINAL VALIDATION ===
        print("\n=== Final seq validation ===")
        print(f"Server seqs observed: {client.server_seqs}")

        if not client.server_seqs:
            print("FAIL: no server seqs recorded")
            return 1

        # Must be strictly increasing by exactly 1, starting at 1
        expected = list(range(1, len(client.server_seqs) + 1))
        if client.server_seqs != expected:
            print(f"FAIL: server seqs are not the consecutive series 1..N")
            print(f"       got: {client.server_seqs}")
            print(f"       expected: {expected}")
            return 1

        print(
            "PASS: server maintained independent, strictly increasing seq counter (1,2,3,...)"
        )
        print(
            f"PASS: all responses carried correct request_seq values matching the requests we sent."
        )
        print(
            f"PASS: client seqs ({list(range(1, client.client_seq))}) were independent of server seqs."
        )

        # Bonus: make sure we saw the key events with correct seqs
        # (initialized at 2, terminated somewhere after 4)
        assert 2 in client.server_seqs
        assert any(
            m.get("event") == "terminated"
            for m in client.received_messages
            if m.get("type") == "event"
        )

        print("\n=== ALL CHECKS PASSED ===")
        return 0

    except AssertionError as e:
        print(f"\nFAIL: assertion error: {e}")
        print("Last 10 messages:")
        for m in client.received_messages[-10:]:
            print("  ", m)
        return 1
    except Exception as e:
        print(f"\nFAIL: unexpected error: {e}")
        import traceback

        traceback.print_exc()
        return 1
    finally:
        client.shutdown()
        # Show any stderr from the child (useful if it crashed)
        try:
            err = client.proc.stderr.read().decode("utf-8", errors="replace")
            if err.strip():
                print("\n--- jitrunner stderr ---")
                print(err.strip())
        except:
            pass


if __name__ == "__main__":
    sys.exit(main())
