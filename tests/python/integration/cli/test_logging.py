"""Logging contracts against a built CLI, with independent subprocesses."""
import json
import subprocess

import pytest

pytestmark = [pytest.mark.integration, pytest.mark.cli]


@pytest.fixture
def run_snapshot(binary, tmp_path, test_environment):
    def run(*before):
        return subprocess.run([str(binary), *before, "mirror", "--test-pattern", "--snapshot",
                               "--snapshot-delay", "0", "--width", "24", "--height", "12",
                               "--audio=false", "--color-mode", "none", "--strip-ansi"],
                              cwd=tmp_path, env=test_environment, capture_output=True, text=True, timeout=25)
    return run


def assert_snapshot(process):
    assert process.returncode == 0, process.stderr[-3000:]
    assert process.stdout.strip(), "Successful mirror produced no frame"
    assert chr(27) not in process.stdout


def test_json_logging_uses_explicit_filename_and_keeps_snapshot_plain(run_snapshot, tmp_path):
    destination = tmp_path / "explicit.json"
    process = run_snapshot("--json", "--log-file", str(destination), "--log-level", "debug")
    assert_snapshot(process)
    records = [json.loads(line) for line in destination.read_text().splitlines() if line.strip()]
    assert records
    assert all(isinstance(record, dict) for record in records)
    assert any("message" in record.get("body", {}) for record in records)


def test_custom_template_and_grep_preserve_frame_output(run_snapshot, tmp_path):
    log_file = tmp_path / "template.log"
    process = run_snapshot("--log-template", "contract:%level:%message", "--log-format-console",
                           "--log-file", str(log_file), "--log-level", "debug", "--grep", "/.*/i",
                           "--color-scheme", "pastel")
    assert_snapshot(process)
    assert "contract:" in process.stderr
    assert log_file.exists()
    assert "contract:" not in log_file.read_text()
    assert "contract:" not in process.stdout


def test_invalid_grep_and_unwritable_json_destination_fail_before_rendering(run_snapshot, tmp_path):
    process = run_snapshot("--grep", "/unterminated")
    assert process.returncode != 0
    assert not process.stdout.strip()
    destination = tmp_path / "missing" / "output.json"
    process = run_snapshot("--json", "--log-file", str(destination))
    assert process.returncode != 0
    assert not destination.exists()
