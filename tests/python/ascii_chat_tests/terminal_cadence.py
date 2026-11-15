"""Window bounds and elapsed-time checks for visible terminal animation."""


def probe_box(
    client: tuple[int, int, int, int], point: tuple[int, int]
) -> tuple[int, int, int, int]:
    """Keep the complete sampling region inside the terminal client area."""
    left, top, right, bottom = client
    if right <= left or bottom <= top:
        raise ValueError(f"Invalid terminal client bounds: {client}")
    width, height = min(640, right - left), min(400, bottom - top)
    x = min(max(point[0] - width // 2, 0), right - left - width)
    y = min(max(point[1] - height // 2, 0), bottom - top - height)
    return left + x, top + y, left + x + width, top + y + height


def cadence_metrics(observations: list[tuple[float, bool]], duration: float) -> dict[str, float]:
    """Count displayed changes over wall time, including a final frozen period."""
    if duration <= 0:
        raise ValueError("Capture duration must be positive")
    last_change = 0.0
    longest_stall = 0.0
    changes = 0
    for elapsed, changed in observations:
        if changed:
            longest_stall = max(longest_stall, elapsed - last_change)
            last_change = elapsed
            changes += 1
    return {
        "duration_seconds": duration,
        "probe_fps": len(observations) / duration,
        "visible_fps": changes / duration,
        "longest_unchanged_seconds": max(longest_stall, duration - last_change),
    }


def require_cadence(metrics: dict[str, float], fps: float) -> None:
    """Require the requested visible cadence and reject prolonged stalls."""
    if metrics["probe_fps"] < fps * 0.9:
        raise AssertionError(f"Capture probe could not sustain sampling cadence: {metrics}")
    if metrics["visible_fps"] < fps * 5 / 6 or metrics["longest_unchanged_seconds"] > 0.1:
        raise AssertionError(
            f"Alacritty visible cadence failed (minimum {fps * 5 / 6:.1f} FPS, maximum stall 0.1s): {metrics}"
        )
