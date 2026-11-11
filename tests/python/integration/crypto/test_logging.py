"""Verify production crypto diagnostics do not leak secrets."""
import subprocess

import pytest

pytestmark = pytest.mark.integration


def test_crypto_logging(crypto_logging_probe, tmp_path, test_environment):
    log = tmp_path / "crypto.log"
    result = subprocess.run([str(crypto_logging_probe), str(log)], capture_output=True, timeout=30,
                            env=test_environment)
    assert result.returncode == 0, (result.stdout + result.stderr).decode("utf-8", errors="replace")
    output = log.read_text(encoding="utf-8")
    assert "b7" * 8 not in output
    assert "logging-test-private-payload" not in output
    assert "Key exchange complete" in output
    assert 1 <= output.count("Encrypting packet:") < 20
    assert 2 <= output.count("Decryption failed: cipherlen=") < 20
    assert "key_exchange_complete=1" in output
