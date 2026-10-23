"""Check production crypto diagnostics for bounded volume and secret redaction.

Build test-crypto-logging, then pass its path to this script.
"""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401

from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="ascii-logging-") as tmp:
    log = Path(tmp) / "crypto.log"
    subprocess.run([str(Path(sys.argv[1]).resolve()), str(log)], check=True, timeout=30)
    output = log.read_text(encoding="utf-8")
    assert "b7" * 8 not in output, "Secret key material appeared in diagnostics"
    assert "logging-test-private-payload" not in output, "Plaintext appeared in diagnostics"
    assert "Key exchange complete" in output, "Handshake troubleshooting event missing"
    encrypt_count = output.count("Encrypting packet:")
    failures = output.count("Decryption failed: cipherlen=")
    assert 1 <= encrypt_count < 20, f"Unexpected encryption log volume: {encrypt_count}"
    assert 2 <= failures < 20, f"Failure logs were unbounded or permanently suppressed: {failures}"
    assert "key_exchange_complete=1" in output, "Failure context missing"
    print(f"PASS: 1000 round trips; {encrypt_count} packet diagnostics; "
          f"1001 bad MACs, {failures} failure diagnostics; no keys or plaintext logged")
