"""Run ascii-chat in Alacritty and verify that its monitor output changes."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import time
from pathlib import Path
from typing import cast

import pygetwindow as gw
import pymonctl as pm
import psutil
import mss
import win32con
import win32api
import win32gui
import win32process
import pywintypes
from PIL import Image, ImageChops, ImageStat
from ascii_chat_tests.terminal_cadence import cadence_metrics, probe_box, require_cadence

ROOT = Path(__file__).resolve().parents[1]
ALACRITTY = Path(r"C:\Program Files\Alacritty\alacritty.exe")
SCREEN = mss.MSS()


def alacritty_windows(title: str) -> list[object]:
    return [window for window in gw.getWindowsWithTitle(title) if window.visible]


def process_window(pid: int) -> int | None:
    handles: list[int] = []

    def collect(hwnd: int, _extra: object) -> None:
        if win32gui.IsWindowVisible(hwnd) and win32process.GetWindowThreadProcessId(hwnd)[1] == pid:
            handles.append(hwnd)

    win32gui.EnumWindows(collect, None)
    return handles[-1] if handles else None


def titled_window(title: str) -> int | None:
    handles: list[int] = []

    def collect(hwnd: int, _extra: object) -> None:
        if win32gui.IsWindowVisible(hwnd) and win32gui.GetWindowText(hwnd) == title:
            handles.append(hwnd)

    win32gui.EnumWindows(collect, None)
    return handles[-1] if handles else None


def force_terminate_alacritty() -> None:
    result = subprocess.run(["taskkill", "/F", "/T", "/IM", "alacritty.exe"], capture_output=True, text=True, check=False)
    if result.returncode not in (0, 128):
        raise RuntimeError(f"taskkill failed: {result.stdout.strip()} {result.stderr.strip()}")
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if not any(process.info["name"] and process.info["name"].lower() == "alacritty.exe" for process in psutil.process_iter(["name"])):
            return
        time.sleep(0.1)
    raise RuntimeError("old Alacritty windows remained after forced termination")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--alacritty", type=Path, default=ALACRITTY, help="Alacritty executable to test")
    parser.add_argument("--monitor-index", type=int, default=0, help="PyMonCtl monitor index (default: 0)")
    parser.add_argument("--binary", type=Path, default=ROOT / "build-fast-debug" / "bin" / "ascii-chat.exe")
    parser.add_argument("--output-dir", type=Path, default=ROOT)
    parser.add_argument("--samples", type=int, default=1, help="number of frame comparisons (default: 1)")
    parser.add_argument("--interval", type=float, default=3.0, help="seconds between comparisons (default: 3)")
    parser.add_argument("--fps", type=float, default=60.0, help="render cadence checked during the middle of the test (default: 60)")
    parser.add_argument("--fps-seconds", type=float, default=2.0, help="seconds of high-rate frame checks (default: 2)")
    parser.add_argument("--no-fps-check", action="store_true", help="only perform the legacy three-second A/B comparison")
    parser.add_argument("--hold-seconds", type=float, default=0.0, help="keep the test window visible before cleanup")
    parser.add_argument(
        "--ascii-args",
        nargs=argparse.REMAINDER,
        default=["mirror", "-c", "0", "--color-mode", "truecolor"],
    )
    return parser.parse_args()


def focus_window(hwnd: int) -> None:
    foreground = win32gui.GetForegroundWindow()
    current_thread = win32api.GetCurrentThreadId()
    foreground_thread = win32process.GetWindowThreadProcessId(foreground)[0] if foreground else 0
    attached = foreground_thread and foreground_thread != current_thread
    if attached:
        win32process.AttachThreadInput(current_thread, foreground_thread, True)
    try:
        win32gui.BringWindowToTop(hwnd)
        win32gui.SetForegroundWindow(hwnd)
        win32gui.SetFocus(hwnd)
    finally:
        if attached:
            win32process.AttachThreadInput(current_thread, foreground_thread, False)


def client_signature(box: tuple[int, int, int, int]) -> bytes:
    # Capture one small region around the image-difference location selected
    # from the two diagnostic frames. A GDI GetPixel loop is too slow to be a
    # 60 Hz probe and can sample a static cell of the pattern.
    left, top, right, bottom = box
    shot = SCREEN.grab({
        "left": left,
        "top": top,
        "width": right - left,
        "height": bottom - top,
    })
    return bytes(shot.raw)


def capture_image(box: tuple[int, int, int, int]) -> Image.Image:
    left, top, right, bottom = box
    shot = SCREEN.grab({"left": left, "top": top, "width": right - left, "height": bottom - top})
    return Image.frombytes("RGB", shot.size, shot.rgb)


def main() -> None:
    args = parse_args()
    if not args.alacritty.is_file():
        raise RuntimeError(f"Alacritty was not found at {args.alacritty}")
    if not args.binary.is_file():
        raise RuntimeError(f"ascii-chat binary was not found at {args.binary}")
    if args.samples < 1 or args.interval <= 0 or args.fps <= 0 or args.fps_seconds <= 0:
        raise RuntimeError("samples, interval, fps, and fps-seconds must be positive")

    ascii_args_lower = [value.lower() for value in args.ascii_args]
    explicit_ascii_fps = any(value == "--fps" or value.startswith("--fps=") for value in ascii_args_lower)
    ascii_fps = int(args.fps)

    args.output_dir.mkdir(parents=True, exist_ok=True)
    force_terminate_alacritty()
    monitors = pm.getAllMonitors()
    if args.monitor_index < 0 or args.monitor_index >= len(monitors):
        raise RuntimeError(f"monitor index {args.monitor_index} is unavailable; found {len(monitors)} monitors")
    target = monitors[args.monitor_index]
    target_rect = (target.rect.left, target.rect.top, target.rect.right, target.rect.bottom)
    window_title = f"ascii-chat-alacritty-integration-{os.getpid()}"
    command = [
        str(args.alacritty.resolve()),
        "--title",
        window_title,
        "--working-directory",
        str(ROOT),
        "-e",
        str(args.binary),
        *args.ascii_args,
    ]
    if not explicit_ascii_fps:
        command.extend(["--fps", str(ascii_fps)])
    process = subprocess.Popen(command)
    try:
        deadline = time.monotonic() + 10
        window = None
        while time.monotonic() < deadline:
            live_hwnd = titled_window(window_title)
            if live_hwnd:
                window = live_hwnd
                break
            time.sleep(0.1)
        if window is None:
            raise RuntimeError("Alacritty did not create a visible window")

        left, top, right, bottom = target_rect
        hwnd = window
        win32gui.ShowWindow(hwnd, win32con.SW_RESTORE)
        flags = win32con.SWP_SHOWWINDOW | win32con.SWP_NOACTIVATE
        win32gui.SetWindowPos(hwnd, win32con.HWND_TOPMOST, left, top, right - left, bottom - top, flags)
        win32gui.SetWindowPos(hwnd, win32con.HWND_NOTOPMOST, left, top, right - left, bottom - top, flags)
        # Winit can replace the native handle while Alacritty finishes
        # creating the surface. Reacquire by the unique title before any
        # focus/maximize operation so all subsequent checks use the live HWND.
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            current_hwnd = titled_window(window_title)
            if current_hwnd:
                hwnd = current_hwnd
                break
            time.sleep(0.05)
        else:
            raise RuntimeError("Alacritty replaced its window handle before arrangement")
        win32gui.ShowWindow(hwnd, win32con.SW_RESTORE)
        win32gui.SetWindowPos(hwnd, win32con.HWND_TOPMOST, left, top, right - left, bottom - top, flags)
        win32gui.SetWindowPos(hwnd, win32con.HWND_NOTOPMOST, left, top, right - left, bottom - top, flags)
        focus_deadline = time.monotonic() + 3
        foreground_hwnd = 0
        while time.monotonic() < focus_deadline:
            current_hwnd = titled_window(window_title)
            if current_hwnd:
                hwnd = current_hwnd
                try:
                    win32gui.BringWindowToTop(hwnd)
                    focus_window(hwnd)
                    foreground_hwnd = win32gui.GetForegroundWindow()
                    if foreground_hwnd == hwnd:
                        break
                except pywintypes.error:
                    pass
            time.sleep(0.05)
        # Foreground activation is restricted by Windows when the Codex
        # process is not the active desktop owner. It is not required for
        # moving, maximizing, or capturing the window, so treat it as best
        # effort and make the maximize operation authoritative.
        win32gui.ShowWindow(hwnd, win32con.SW_MAXIMIZE)
        if win32gui.GetWindowPlacement(hwnd)[1] != win32con.SW_SHOWMAXIMIZED:
            raise RuntimeError(f"Alacritty did not maximize: hwnd={hwnd} rect={win32gui.GetWindowRect(hwnd)}")
        actual = win32gui.GetWindowRect(hwnd)
        if not (actual[0] < right and actual[2] > left and actual[1] < bottom and actual[3] > top):
            raise RuntimeError(f"Alacritty window was not placed on monitor {args.monitor_index}: {actual}")
        monitor_handle = win32api.MonitorFromWindow(hwnd, win32con.MONITOR_DEFAULTTONEAREST)
        monitor_info = win32api.GetMonitorInfo(monitor_handle)
        if tuple(monitor_info["Monitor"]) != target_rect:
            raise RuntimeError(
                f"Alacritty resolved to the wrong monitor: expected={target_rect} "
                f"actual={tuple(monitor_info['Monitor'])} window={actual}"
            )

        client_width, client_height = win32gui.GetClientRect(hwnd)[2:]
        client_left, client_top = win32gui.ClientToScreen(hwnd, (0, 0))
        client_right, client_bottom = win32gui.ClientToScreen(hwnd, (client_width, client_height))
        box = (client_left, client_top, client_right, client_bottom)
        frame_a = args.output_dir / "alacritty-monitor-a.png"
        frame_b = args.output_dir / "alacritty-monitor-b.png"
        ready_deadline = time.monotonic() + 10
        previous = None
        while time.monotonic() < ready_deadline:
            candidate = capture_image(box)
            red, green, blue = candidate.split()
            intensity = ImageChops.lighter(ImageChops.lighter(red, green), blue)
            bright_pixels = sum(intensity.histogram()[81:])
            if bright_pixels / (candidate.width * candidate.height) >= 0.005:
                previous = candidate
                break
            time.sleep(0.1)
        if previous is None:
            raise AssertionError("Alacritty window never produced visible content")
        previous.save(frame_a)
        samples = []
        if args.no_fps_check:
            for index in range(args.samples):
                time.sleep(args.interval)
                current = capture_image(box)
                difference = ImageChops.difference(previous, current)
                samples.append((index, difference.getbbox() is not None, sum(ImageStat.Stat(difference).mean) / 3))
                previous = current
        else:
            # Full-monitor grabs are intentionally limited to the two artifacts.
            # Direct Win32 pixel reads keep the 60 Hz probe independent of the
            # much slower full-screen screenshot path.
            # Let Alacritty finish its ConPTY surface setup and discard the
            # initial resize/startup frames before measuring cadence.
            time.sleep(1.0)
            current_hwnd = titled_window(window_title)
            if current_hwnd:
                hwnd = current_hwnd
                client_width, client_height = win32gui.GetClientRect(hwnd)[2:]
                client_left, client_top = win32gui.ClientToScreen(hwnd, (0, 0))
                client_right, client_bottom = win32gui.ClientToScreen(hwnd, (client_width, client_height))
                box = (client_left, client_top, client_right, client_bottom)
            probe_frame = capture_image(box)
            changed_box = ImageChops.difference(previous, probe_frame).getbbox()
            if changed_box:
                changed_left, changed_top, changed_right, changed_bottom = changed_box
                diff_pixels = ImageChops.difference(previous, probe_frame).load()
                if diff_pixels is None:
                    raise RuntimeError("Could not inspect captured frame pixels")
                strongest = (0, changed_left, changed_top)
                for y in range(changed_top, changed_bottom, 4):
                    for x in range(changed_left, changed_right, 4):
                        pixel = cast(tuple[int, int, int], diff_pixels[x, y])
                        strength = max(pixel)
                        if strength > strongest[0]:
                            strongest = (strength, x, y)
                points = [(strongest[1], strongest[2])]
            else:
                points = [((client_right - client_left) // 2, (client_bottom - client_top) // 2)]
            region = probe_box(box, points[0])
            probe_previous = client_signature(region)
            interval = 1.0 / args.fps
            start_time = time.monotonic()
            deadline = start_time + args.fps_seconds
            index = 0
            observations = []
            while time.monotonic() < deadline:
                target_time = start_time + (index + 1) * interval
                remaining = target_time - time.monotonic()
                if remaining > 0:
                    time.sleep(remaining)
                probe_current = client_signature(region)
                observed_at = time.monotonic() - start_time
                changed = probe_current != probe_previous
                observations.append((observed_at, changed))
                samples.append((index, changed, 1.0 if changed else 0.0))
                probe_previous = probe_current
                # Skip missed sampling slots instead of counting a burst of
                # catch-up captures as independently displayed frames.
                index = max(index + 1, int(observed_at / interval))
            metrics = cadence_metrics(observations, time.monotonic() - start_time)
            print("cadence=" + json.dumps(metrics, sort_keys=True))
            current = capture_image(box)
        current.save(frame_b)
        print(f"monitor_index={args.monitor_index} monitor={target_rect}")
        print(f"window={actual} hwnd={hwnd} pid={process.pid} title={win32gui.GetWindowText(hwnd)!r}")
        print(f"frame_a={frame_a}\nframe_b={frame_b}")
        print("samples=" + ",".join(f"{i}:{'changed' if changed else 'same'}:{mean:.2f}" for i, changed, mean in samples))
        if args.hold_seconds > 0:
            time.sleep(args.hold_seconds)
        changed_count = sum(1 for _, changed, _ in samples if changed)
        required_changed = 1 if args.no_fps_check else len(samples) * 5 / 6
        if not args.no_fps_check:
            require_cadence(metrics, args.fps)
        if changed_count < required_changed:
            unchanged = [index for index, changed, _ in samples if not changed]
            preview = ", ".join(str(index) for index in unchanged[:12])
            suffix = "..." if len(unchanged) > 12 else ""
            raise AssertionError(
                f"Alacritty capture changed only {changed_count}/{len(samples)} frames; "
                f"required at least {required_changed} changed frames "
                f"(unchanged indices {preview}{suffix})"
            )
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(process.pid)], check=False)
            process.wait(timeout=5)


if __name__ == "__main__":
    main()
