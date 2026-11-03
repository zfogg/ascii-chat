"""Run the TLS identity fixture with disposable generated credentials."""

from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="ascii-chat-quic-tls-identity-") as temporary:
    directory = Path(temporary)
    arguments = [directory / name for name in ("server.pem", "server.key", "other.pem", "other.key")]
    result = subprocess.run([sys.argv[1], *(str(path) for path in arguments)], timeout=30, check=False)
    raise SystemExit(result.returncode)
