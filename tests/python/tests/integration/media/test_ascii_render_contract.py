"""Shared terminal rendering contract across important mirror configurations."""

import pytest

pytestmark = [pytest.mark.integration, pytest.mark.native, pytest.mark.media]

CASES = [
    ("standard", []), ("glyph", ["--renderer", "glyph"]), ("standard", ["--stabilize"]),
    ("standard", ["--flip-x"]), ("standard", ["--flip-y"]), ("standard", ["--stretch"]),
    ("standard", ["--stretch", "--flip-x"]), ("standard", ["--stabilize", "--flip-y"]),
    ("standard", ["--palette", "standard"]), ("standard", ["--palette", "minimal"]),
    ("standard", ["--palette", "digital"]), ("standard", ["--palette", "cool"]),
    ("glyph", ["--renderer", "glyph", "--palette", "minimal"]),
    ("glyph", ["--renderer", "glyph", "--flip-x"]), ("glyph", ["--renderer", "glyph", "--flip-y"]),
    ("glyph", ["--renderer", "glyph", "--stabilize"]), ("standard", ["--color-mode", "none"]),
    ("standard", ["--utf8", "true"]), ("standard", ["--fps", "24"]), ("standard", ["--fps", "30"]),
]


@pytest.mark.parametrize("palette, extra_args", CASES)
def test_mirror_render_contract(ensure_can_render, palette, extra_args):
    ensure_can_render("mirror", palette, "1", *extra_args)
