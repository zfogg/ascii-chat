"""Focused native watchdog smoke: actual mutex cycle, no camera or network peers.
Run: python tests/manual/test_otel_watchdog.py build/bin/asciichat.dll
Uses oversized aligned opaque mutex storage to exercise the exported platform API.
"""
import ctypes
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "deps/ascii-chat-deps/gotel/c-gotel/testing/python"))
from gotel_testing import Collector, records, protobuf


def child(library, endpoint):
    os.environ["ASCII_CHAT_OTEL_ENDPOINT"] = endpoint
    os.environ["ASCII_CHAT_OTEL_METRICS_INTERVAL"] = "1"
    dll = ctypes.CDLL(str(Path(library).resolve()))
    assert dll.asciichat_shared_init(None, False, True) == 0
    argv = (ctypes.c_char_p * 3)(b"ascii-chat", b"server", None)
    assert dll.options_init(2, argv) == 0
    assert dll.ascii_otel_init(b"server", 2, argv) == 0
    locks = [(ctypes.c_uint64 * 1024)() for _ in range(2)]
    for i, lock in enumerate(locks):
        assert dll.mutex_init(lock, ("watchdog-smoke-%d" % i).encode()) == 0
    # A long wait without a cycle must not generate an error.
    dll.mutex_lock_impl(locks[0])
    healthy = threading.Thread(target=lambda: (dll.mutex_lock_impl(locks[0]), dll.mutex_unlock_impl(locks[0])))
    healthy.start()
    time.sleep(3.5)
    dll.mutex_unlock_impl(locks[0])
    healthy.join(3)
    assert not healthy.is_alive()
    print("healthy", flush=True)
    barrier = threading.Barrier(2)
    def blocked(index):
        scope = (ctypes.c_uint64 * 128)()
        dll.ascii_otel_scope_begin(scope, 3)  # CLI, a non-realtime operation.
        class Context(ctypes.Structure):
            _fields_ = [("trace_id", ctypes.c_ubyte * 16), ("span_id", ctypes.c_ubyte * 8),
                        ("valid", ctypes.c_bool), ("sampled", ctypes.c_bool), ("realtime", ctypes.c_bool)]
        dll.ascii_otel_context.restype = Context
        context = dll.ascii_otel_context()
        print("parent:" + bytes(context.trace_id).hex() + ":" + bytes(context.span_id).hex(), flush=True)
        dll.errno_set_with_message(1, b"watchdog-smoke", 1, b"blocked", b"watchdog fixture pending error")
        dll.mutex_lock_impl(locks[index])
        barrier.wait()
        dll.mutex_lock_impl(locks[1-index])
    for i in range(2):
        threading.Thread(target=blocked, args=(i,), daemon=True).start()
    time.sleep(9)
    # The blocked threads cannot cooperate with shutdown; the watchdog must
    # export before process termination. Parent kills only this disposable child.
    print("blocked", flush=True)
    time.sleep(60)


def main():
    if len(sys.argv) == 4 and sys.argv[1] == "--child":
        child(sys.argv[2], sys.argv[3])
        return
    with Collector() as receiver:
        child_env = os.environ.copy()
        if sys.platform == "darwin" and child_env.get("WATCHDOG_ASAN_LIBRARY"):
            child_env["DYLD_INSERT_LIBRARIES"] = child_env["WATCHDOG_ASAN_LIBRARY"]
        process = subprocess.Popen([sys.executable, __file__, "--child", sys.argv[1], receiver.url],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=child_env)
        deadline = threading.Timer(30, process.kill)
        deadline.start()
        try:
            assert process.stdout.readline().strip() == "healthy"
            healthy = [s for s in records(receiver.records, "/v1/traces") if s.get(5) == [b"ascii-chat.thread.deadlock"]]
            assert not healthy, "ordinary contention was reported as deadlock"
            parents = []
            for _ in range(2):
                line = process.stdout.readline().strip().split(":")
                assert line[0] == "parent", line
                parents.append((line[1], line[2]))
            assert process.stdout.readline().strip() == "blocked"
            spans = [s for s in records(receiver.records, "/v1/traces") if s.get(5) == [b"ascii-chat.thread.deadlock"]]
            assert len(spans) == 1, ("missing report or repeated-report flood", len(spans))
            span = spans[0]
            assert (span[1][0].hex(), span[4][0].hex()) in parents, "watchdog must be a child in the stalled trace"
            assert protobuf(span[15][0])[3] == [2], "watchdog span must have ERROR status"
            attributes = {protobuf(a)[1][0].decode(): protobuf(protobuf(a)[2][0]) for a in span.get(9, [])}
            assert attributes["thread.deadlock.count"][3] == [2], attributes
            assert attributes["thread.blocked.span.name"][1] == [b"ascii-chat.cli"], attributes
            assert attributes["asciichat_errno.code"][3] == [1], attributes
            assert attributes["asciichat_errno.message"][1] == [b"watchdog fixture pending error"], attributes
            assert attributes["asciichat_errno.code_message"][1][0], attributes
            assert attributes["error.type"][1] == [b"thread.deadlock"], attributes
            parent_spans = [p for p in records(receiver.records, "/v1/traces")
                            if p.get(1) == span[1] and p.get(2) == span[4]]
            assert len(parent_spans) == 1, "blocked parent snapshot is missing or duplicated"
            parent = parent_spans[0]
            assert parent[5] == [b"ascii-chat.cli"]
            parent_attributes = {protobuf(a)[1][0].decode(): protobuf(protobuf(a)[2][0])
                                 for a in parent.get(9, [])}
            assert parent_attributes["gotel.span.snapshot"][2] == [1]
            assert parent_attributes["gotel.span.incomplete"][2] == [1]
            assert "ascii-chat.thread.deadlock.threads" in [m[1][0].decode() for m in records(receiver.records, "/v1/metrics")]
            print(json.dumps({"trace_id": span[1][0].hex(), "span_id": span[2][0].hex(), "deadlocked_threads": 2,
                              "healthy_contention": "passed", "live_export": "passed", "rate_limit": "passed"}))
            if os.environ.get("WATCHDOG_REPLAY_ENDPOINT"):
                import urllib.request
                for route in ("/v1/traces", "/v1/metrics"):
                    payload = b"".join(body for path, body in receiver.records if path == route)
                    if payload:
                        urllib.request.urlopen(urllib.request.Request(os.environ["WATCHDOG_REPLAY_ENDPOINT"] + route,
                            data=payload, headers={"Content-Type": "application/x-protobuf"}), timeout=10).close()
        finally:
            deadline.cancel()
            process.kill()
            _, errors = process.communicate(timeout=10)
            if not locals().get('spans'):
                print(errors[-4000:], file=sys.stderr)

if __name__ == "__main__":
    main()
