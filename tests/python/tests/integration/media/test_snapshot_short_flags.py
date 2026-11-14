"""Regression coverage for mirror snapshot short flags."""

import pytest


pytestmark = [pytest.mark.integration, pytest.mark.native]


def test_mirror_short_snapshot_flags_render_in_terminal(ensure_can_render):
    ensure_can_render(palette="standard", delay="0")
    ensure_can_render(palette="standard", delay="5")
