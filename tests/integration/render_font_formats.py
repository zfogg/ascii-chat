"""Check CLI font failures and optional real Apple HVF rendering without hardware."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import _environment  # noqa: F401,E402
from ascii_chat_tests.cli import run_cli
from ascii_chat_tests.cli import cli_command

import os
import struct
import subprocess
import tempfile


def main():
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="ascii-chat-font-formats-") as directory:
        root = Path(directory)

        def render(font, name):
            output = root / f"{name}.png"
            result = run_cli(binary, "mirror", "--test-pattern", "--audio=false",
                 "--snapshot", "--snapshot-delay", "0", "--splash-screen=false",
                 "--width", "40", "--height", "20", "--render-font", str(font),
                 "--render-file", str(output), capture_output=True, timeout=45, cwd=root)
            return result, output

        def supported(font, name):
            result, output = render(font, name)
            assert result.returncode == 0, (result.returncode, result.stderr.decode(errors="replace"))
            data = output.read_bytes()
            assert data[:8] == b"\x89PNG\r\n\x1a\n", output
            width, height = struct.unpack(">II", data[16:24])
            assert width > 0 and height > 0, (width, height)
            print(f"PASS: {name} rendered {width}x{height}")

        supported("default", "bundled")
        invalid = root / "unsupported.ttf"
        invalid.write_bytes(b"This is not a supported font format\n")
        result, output = render(invalid, "unsupported")
        text = (result.stdout + result.stderr).decode(errors="replace")
        assert result.returncode != 0 and "Unsupported font format" in text, text
        assert not output.exists(), "Unsupported font silently produced output"
        print("PASS: unsupported font exits with an explicit format error")

        result, output = render(invalid.name, "unsupported-relative")
        text = (result.stdout + result.stderr).decode(errors="replace")
        assert result.returncode != 0 and "Unsupported font format" in text, text
        assert not output.exists(), "Relative unsupported font silently produced output"
        print("PASS: relative font files are validated rather than resolved as family names")

        result, output = render(root / "missing.ttf", "missing")
        assert result.returncode != 0 and not output.exists(), "Missing explicit font fell back to default"
        print("PASS: missing explicit font does not fall back")

        result, output = render(os.path.join(".", "missing.ttf"), "missing-relative")
        assert result.returncode != 0 and not output.exists(), "Missing relative path fell back to default"

        hvf = os.environ.get("ASCIICHAT_TEST_HVF_FONT")
        if not hvf and sys.platform == "darwin":
            candidate = Path("/System/Library/PrivateFrameworks/FontServices.framework/Versions/A/Resources/Reserved/PingFangUI.ttc")
            if candidate.is_file():
                hvf = str(candidate)
        if hvf:
            if sys.platform == "darwin":
                supported(hvf, "hvf")
            else:
                result, output = render(hvf, "hvf")
                text = (result.stdout + result.stderr).decode(errors="replace")
                assert result.returncode != 0 and "Unsupported" in text and "HVF" in text, text
                assert not output.exists(), "Unsupported HVF silently produced output"
                print("PASS: HVF is explicitly rejected on this platform")
        else:
            print("SKIP: real HVF font not available (set ASCIICHAT_TEST_HVF_FONT)")


if __name__ == "__main__":
    main()
