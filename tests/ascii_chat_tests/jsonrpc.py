"""Bounded JSON-lines subprocess transport for MCP integration tests."""
import json
import queue
import subprocess
import threading
import time
from typing import IO, cast


class JsonRpcProcess:
    """Exchange JSON messages with a child process and capture its stderr."""

    def __init__(self, command, stderr_path, *, cwd=None, env=None, count_notifications=True):
        self.stderr = open(stderr_path, "wb")
        try:
            self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                            stderr=self.stderr, cwd=cwd, env=env)
        except BaseException:
            self.stderr.close()
            raise
        self.stdin = cast(IO[bytes], self.process.stdin)
        self.stdout = cast(IO[bytes], self.process.stdout)
        assert self.stdin is not None and self.stdout is not None
        self.lines = queue.Queue()
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()
        self.sequence = 0
        self.count_notifications = count_notifications

    def _read(self):
        try:
            for line in self.stdout:
                self.lines.put(line)
        finally:
            self.lines.put(None)

    def send_message(self, message):
        self.stdin.write((json.dumps(message) + "\n").encode("utf-8"))
        self.stdin.flush()

    def receive(self, timeout=90):
        try:
            line = self.lines.get(timeout=timeout)
        except queue.Empty as exc:
            raise TimeoutError("Timed out waiting for JSON-RPC response") from exc
        if line is None:
            raise EOFError(f"JSON-RPC process exited (status {self.process.poll()})")
        try:
            return json.loads(line)
        except (ValueError, UnicodeDecodeError) as exc:
            raise ValueError(f"Non-JSON stdout: {line!r}") from exc

    def request(self, method, params=None, *, notification=False, timeout=90, on_message=None):
        if not notification or self.count_notifications:
            self.sequence += 1
        message = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            message["params"] = params
        if not notification:
            message["id"] = self.sequence
        if on_message:
            on_message("request", message)
        self.send_message(message)
        if notification:
            return None
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f"Timed out waiting for {method}")
            response = self.receive(remaining)
            if on_message:
                on_message("response", response)
            if response.get("id") == message["id"]:
                return response

    def close(self):
        try:
            try:
                self.stdin.close()
            except (BrokenPipeError, OSError):
                pass
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=10)
        finally:
            self.reader.join(timeout=2)
            self.stdout.close()
            self.stderr.close()
