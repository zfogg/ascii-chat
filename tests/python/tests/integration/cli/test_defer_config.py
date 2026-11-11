"""Exercise deferred TOML cleanup through the native executable."""

import os
from pathlib import Path
import subprocess

import pytest
from ascii_chat_tests.cli import clean_environment, cli_command


@pytest.mark.integration
def test_deferred_configuration_cleanup(binary: Path, tmp_path: Path) -> None:
    image = tmp_path / "image.ppm"
    image.write_bytes(b"P6\n16 16\n255\n" + bytes([255, 128, 0]) * 256)
    valid = tmp_path / "valid.toml"
    valid.write_text("[terminal]\nwidth = 30\nheight = 12\n", encoding="utf-8")
    invalid = tmp_path / "invalid.toml"
    invalid.write_text("[terminal\nwidth = 30\n", encoding="utf-8")
    env = clean_environment()
    env.update(
        ASCII_CHAT_OTEL_ENABLED_TELEMETRY="none",
        ASCII_CHAT_DISABLE_FIRST_USE="1",
        HOME=str(tmp_path),
        USERPROFILE=str(tmp_path),
        APPDATA=str(tmp_path),
        LOCALAPPDATA=str(tmp_path),
        XDG_CONFIG_HOME=str(tmp_path),
    )
    for path, expected_success in ((valid, True), (invalid, False)):
        result = subprocess.run(
            cli_command(
                os.fspath(binary),
                "--config",
                os.fspath(path),
                "mirror",
                "--file",
                os.fspath(image),
                "--snapshot",
                "--snapshot-delay",
                "0",
                "--splash-screen=false",
                "--audio=false",
                "--color-mode",
                "none",
                "--strip-ansi",
            ),
            capture_output=True,
            text=True,
            env=env,
            timeout=30,
            check=False,
        )
        assert (result.returncode == 0) == expected_success, result.stderr
        assert "AddressSanitizer" not in result.stderr
        if expected_success:
            assert result.stdout.strip()
