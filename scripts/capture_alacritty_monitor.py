"""Run ascii-chat in Alacritty and verify that its monitor output changes."""

from __future__ import annotations

import argparse
import subprocess
import time
from pathlib import Path

import pygetwindow as gw
import pymonctl as pm
import win32con
import win32gui
import pywintypes
from PIL import ImageChops, ImageGrab, ImageStat

ROOT = Path(__file__).resolve().parents[1]
ALACRITTY = Path(r"C:\Program Files\Alacritty\alacritty.exe")


def alacritty_windows() -> list[object]:
    return [window for window in gw.getAllWindows() if "alacritty" in window.title.lower()]


def force_terminate_alacritty() -> None:
    result = subprocess.run(["taskkill", "/F", "/T", "/IM", "alacritty.exe"], capture_output=True, text=True, check=False)
    if result.returncode not in (0, 128):
        raise RuntimeError(f"taskkill failed: {result.stdout.strip()} {result.stderr.strip()}")
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if not alacritty_windows():
            return
        time.sleep(0.1)
    raise RuntimeError("old Alacritty windows remained after forced termination")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--monitor-index", type=int, default=1, help="PyMonCtl monitor index (default: 1)")
    parser.add_argument("--binary", type=Path, default=ROOT / "build-fast-debug" / "bin" / "ascii-chat.exe")
    parser.add_argument("--output-dir", type=Path, default=ROOT)
    parser.add_argument("--samples", type=int, default=1, help="number of frame comparisons (default: 1)")
    parser.add_argument("--interval", type=float, default=3.0, help="seconds between comparisons (default: 3)")
    parser.add_argument("--ascii-args", nargs=argparse.REMAINDER, default=["mirror", "--test-pattern", "1"])
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if not ALACRITTY.is_file():
        raise RuntimeError(f"Alacritty was not found at {ALACRITTY}")
    if not args.binary.is_file():
        raise RuntimeError(f"ascii-chat binary was not found at {args.binary}")
    if args.samples < 1 or args.interval <= 0:
        raise RuntimeError("samples must be positive and interval must be greater than zero")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    force_terminate_alacritty()
    monitors = pm.getAllMonitors()
    if args.monitor_index < 0 or args.monitor_index >= len(monitors):
        raise RuntimeError(f"monitor index {args.monitor_index} is unavailable; found {len(monitors)} monitors")
    target = monitors[args.monitor_index]
    target_rect = (target.rect.left, target.rect.top, target.rect.right, target.rect.bottom)
    command = [str(ALACRITTY), "--working-directory", str(ROOT), "-e", str(args.binary), *args.ascii_args]
    process = subprocess.Popen(command)
    try:
        deadline = time.monotonic() + 10
        window = None
        while time.monotonic() < deadline:
            windows = alacritty_windows()
            if windows:
                window = windows[-1]
                break
            time.sleep(0.1)
        if window is None:
            raise RuntimeError("Alacritty did not create a visible window")

        hwnd = window._hWnd
        left, top, right, bottom = target_rect
        win32gui.ShowWindow(hwnd, win32con.SW_RESTORE)
        win32gui.SetWindowPos(hwnd, win32con.HWND_TOP, left, top, right - left, bottom - top, win32con.SWP_SHOWWINDOW)
        try:
            win32gui.SetForegroundWindow(hwnd)
        except pywintypes.error:
            # Windows may reject foreground activation when the launcher is
            # not the foreground process; placement and maximization still
            # make the capture deterministic.
            pass
        win32gui.ShowWindow(hwnd, win32con.SW_MAXIMIZE)
        actual = win32gui.GetWindowRect(hwnd)
        if not (actual[0] < right and actual[2] > left and actual[1] < bottom and actual[3] > top):
            raise RuntimeError(f"Alacritty window was not placed on monitor {args.monitor_index}: {actual}")

        virtual_left = min(m.rect.left for m in monitors)
        virtual_top = min(m.rect.top for m in monitors)
        client_width, client_height = win32gui.GetClientRect(hwnd)[2:]
        client_left, client_top = win32gui.ClientToScreen(hwnd, (0, 0))
        client_right, client_bottom = win32gui.ClientToScreen(hwnd, (client_width, client_height))
        box = (
            client_left - virtual_left,
            client_top - virtual_top,
            client_right - virtual_left,
            client_bottom - virtual_top,
        )
        frame_a = args.output_dir / "alacritty-monitor-a.png"
        frame_b = args.output_dir / "alacritty-monitor-b.png"
        ready_deadline = time.monotonic() + 10
        previous = None
        while time.monotonic() < ready_deadline:
            candidate = ImageGrab.grab(all_screens=True).crop(box)
            bright_pixels = sum(1 for pixel in candidate.getdata() if max(pixel) > 80)
            if bright_pixels / (candidate.width * candidate.height) >= 0.005:
                previous = candidate
                break
            time.sleep(0.1)
        if previous is None:
            raise AssertionError("Alacritty window never produced visible content")
        previous.save(frame_a)
        samples = []
        for index in range(args.samples):
            time.sleep(args.interval)
            current = ImageGrab.grab(all_screens=True).crop(box)
            difference = ImageChops.difference(previous, current)
            samples.append((index, difference.getbbox() is not None, sum(ImageStat.Stat(difference).mean) / 3))
            previous = current
        current.save(frame_b)
        print(f"monitor_index={args.monitor_index} monitor={target_rect}")
        print(f"window={actual} hwnd={hwnd} pid={process.pid} title={window.title!r}")
        print(f"frame_a={frame_a}\nframe_b={frame_b}")
        print("samples=" + ",".join(f"{i}:{'changed' if changed else 'same'}:{mean:.2f}" for i, changed, mean in samples))
        if not any(changed for _, changed, _ in samples):
            raise AssertionError("Alacritty capture did not change during the integration test")
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(process.pid)], check=False)
            process.wait(timeout=5)


if __name__ == "__main__":
    main()
