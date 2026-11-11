"""Capture native adapter telemetry and verify it offline."""
from pathlib import Path
import shutil
import subprocess

import pytest

pytestmark = pytest.mark.integration


def test_trace_contract(otel_contract_probe, tmp_path, request):
    verifier = request.config.getoption("--spaniel-assert") or shutil.which("spaniel-assert")
    if not verifier:
        pytest.skip("Install the pinned spaniel-assert CLI to verify offline traces")
    capture = tmp_path / "contract.pb"
    spec = Path(__file__).resolve().parents[3] / "integration/otel_contract.json"
    subprocess.run([str(otel_contract_probe), str(capture)], check=True, timeout=60)
    subprocess.run([verifier, "--format", "otlp-proto", "--input", str(capture), "--spec", str(spec)],
                   check=True, timeout=30)
