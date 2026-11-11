"""Check font validation, HVF detection, and Apple HVF rendering."""
import os
from pathlib import Path
import struct
import sys

import pytest
from ascii_chat_tests.cli import run_cli

pytestmark = [pytest.mark.integration, pytest.mark.media]


@pytest.fixture
def render(binary, tmp_path, test_environment):
    def invoke(font):
        output = tmp_path / "output.png"
        result = run_cli(binary, "mirror", "--test-pattern", "--audio=false", "--snapshot", "--snapshot-delay", "0",
                         "--splash-screen=false", "--width", "40", "--height", "20", "--render-font", str(font),
                         "--render-file", str(output), capture_output=True, timeout=45, cwd=tmp_path, env=test_environment)
        return result, output
    return invoke


def check_png(result, output):
    assert result.returncode == 0, result.stderr.decode(errors="replace")
    data = output.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    assert all(value > 0 for value in struct.unpack(">II", data[16:24]))


def test_bundled_font_renders(render):
    check_png(*render("default"))


@pytest.mark.parametrize("relative", [False, True])
def test_unsupported_font_is_rejected(render, tmp_path, relative):
    invalid = tmp_path / "unsupported.ttf"
    invalid.write_bytes(b"This is not a supported font format\n")
    result, output = render(invalid.name if relative else invalid)
    text = (result.stdout + result.stderr).decode(errors="replace")
    assert result.returncode != 0 and "Unsupported font format" in text
    assert not output.exists()


@pytest.mark.parametrize("relative", [False, True])
def test_missing_explicit_font_does_not_fall_back(render, tmp_path, relative):
    result, output = render(os.path.join(".", "missing.ttf") if relative else tmp_path / "missing.ttf")
    assert result.returncode != 0 and not output.exists()


def test_hvf_table_is_rejected_without_hvf_outlines(render, tmp_path):
    # Generate a valid TrueType face carrying an HVF table. This exercises the
    # format guard independently of proprietary system fonts on every OS.
    from fontTools.fontBuilder import FontBuilder
    from fontTools.pens.ttGlyphPen import TTGlyphPen
    from fontTools.ttLib import newTable

    builder = FontBuilder(1000, isTTF=True)
    builder.setupGlyphOrder([".notdef", "space"])
    builder.setupCharacterMap({32: "space"})
    builder.setupGlyf({name: TTGlyphPen(None).glyph() for name in [".notdef", "space"]})
    builder.setupHorizontalMetrics({name: (500, 0) for name in [".notdef", "space"]})
    builder.setupHorizontalHeader(ascent=800, descent=-200)
    builder.setupNameTable({"familyName": "HVF Test", "styleName": "Regular"})
    builder.setupOS2(sTypoAscender=800, sTypoDescender=-200, usWinAscent=800, usWinDescent=200)
    builder.setupPost()
    builder.setupMaxp()
    table = newTable("hvgl")
    table.data = b"\0\0\0\0"
    builder.font["hvgl"] = table
    path = tmp_path / "synthetic-hvf.ttf"
    builder.save(path)
    result, output = render(path)
    text = (result.stdout + result.stderr).decode(errors="replace")
    assert result.returncode != 0 and "Unsupported" in text and "HVF" in text
    assert not output.exists()


if sys.platform == "darwin":
    def test_apple_hvf_font(render):
        hvf = Path(os.environ.get("ASCIICHAT_TEST_HVF_FONT", "/System/Library/PrivateFrameworks/FontServices.framework/Versions/A/Resources/Reserved/PingFangUI.ttc"))
        assert hvf.is_file(), f"Apple HVF font missing: {hvf}"
        check_png(*render(hvf))
