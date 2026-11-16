"""Ensure the desktop integration test rejects frozen or intermittent output."""

import pytest
from PIL import Image, ImageDraw

from ascii_chat_tests.terminal_cadence import cadence_metrics, motion_point, probe_box, require_cadence


@pytest.mark.unit
def test_motion_probe_ignores_isolated_bright_cursor_pixel() -> None:
    before = Image.new("RGB", (1920, 1200))
    after = before.copy()
    after.putpixel((0, 1199), (255, 255, 255))
    ImageDraw.Draw(after).rectangle((960, 0, 1279, 199), fill=(0, 0, 3))
    x, y = motion_point(before, after)
    assert 960 <= x < 1280 and 0 <= y < 200


@pytest.mark.unit
def test_identical_frames_select_client_center() -> None:
    image = Image.new("RGB", (1920, 1200), "blue")
    assert motion_point(image, image) == (960, 600)


@pytest.mark.unit
def test_resize_during_motion_selection_is_rejected() -> None:
    with pytest.raises(ValueError, match="dimensions changed"):
        motion_point(Image.new("RGB", (640, 400)), Image.new("RGB", (1280, 800)))


@pytest.mark.unit
@pytest.mark.parametrize("visible_fps", [50, 60])
def test_requested_visible_cadence_passes(visible_fps: int) -> None:
    observations = []
    previous_frame = 0
    for index in range(1, 121):
        frame = index * visible_fps // 60
        observations.append((index / 60, frame != previous_frame))
        previous_frame = frame
    metrics = cadence_metrics(observations, 2.0)
    assert metrics["visible_fps"] == visible_fps
    require_cadence(metrics, 60)


@pytest.mark.unit
@pytest.mark.parametrize("changes", [0, 30, 60])
def test_freezes_and_slow_animation_fail(changes: int) -> None:
    observations = [(i / 60, i <= changes) for i in range(1, 121)]
    with pytest.raises(AssertionError, match="visible cadence failed"):
        require_cadence(cadence_metrics(observations, 2.0), 60)


@pytest.mark.unit
def test_fast_burst_then_stall_fails_even_with_enough_changes() -> None:
    observations = [(i / 60, i <= 100) for i in range(1, 121)]
    metrics = cadence_metrics(observations, 2.0)
    assert metrics["visible_fps"] == 50
    assert metrics["longest_unchanged_seconds"] == pytest.approx(1 / 3)
    with pytest.raises(AssertionError, match="visible cadence failed"):
        require_cadence(metrics, 60)


@pytest.mark.unit
def test_inadequate_capture_rate_is_inconclusive() -> None:
    with pytest.raises(AssertionError, match="Capture probe"):
        require_cadence(cadence_metrics([(1.0, True), (2.0, True)], 2), 60)


@pytest.mark.unit
@pytest.mark.parametrize("point", [(0, 0), (2559, 1439), (1280, 720)])
def test_probe_cannot_capture_neighboring_windows(point: tuple[int, int]) -> None:
    client = (-3840, -366, -1280, 1074)
    left, top, right, bottom = probe_box(client, point)
    assert client[0] <= left < right <= client[2]
    assert client[1] <= top < bottom <= client[3]
    assert (right - left, bottom - top) == (640, 400)


@pytest.mark.unit
def test_probe_fits_small_client() -> None:
    assert probe_box((10, 20, 110, 100), (99, 79)) == (10, 20, 110, 100)
