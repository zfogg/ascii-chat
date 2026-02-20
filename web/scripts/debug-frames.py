#!/usr/bin/env python3
"""
Frame Debugging Script for ascii-chat Web Client

This script:
1. Navigates to localhost:3000/client using browser-use
2. Monitors ASCII art frame transmission and rendering
3. Tracks FPS, unique frames, and frame statistics
4. Provides real-time debugging output with metrics

Usage:
    python web/scripts/debug-frames.py [--duration 30] [--server ws://localhost:27226] [--interval 1]

Prerequisites:
    - npm run dev (web dev server on port 3000)
    - ./build/bin/ascii-chat server (running on default port)
    - browser-use installed: pip install browser-use
    - ANTHROPIC_API_KEY set in environment
"""

import subprocess
import json
import time
import re
import argparse
import sys
from typing import Optional, Dict, List
from dataclasses import dataclass


@dataclass
class FrameMetrics:
    rendered: int
    received: int
    queue_depth: int
    unique_rendered: int
    frame_hashes: Dict[str, int] = None


def run_browser_use_cmd(cmd_args: List[str], global_opts: List[str] = None) -> str:
    """Execute a browser-use command and return output"""
    if global_opts is None:
        global_opts = []
    try:
        result = subprocess.run(
            ["browser-use"] + global_opts + cmd_args,
            capture_output=True,
            text=True,
            timeout=10
        )
        return result.stdout + result.stderr
    except subprocess.TimeoutExpired:
        return "TIMEOUT"
    except FileNotFoundError:
        print("❌ browser-use not found. Install with: pip install browser-use")
        return ""


def extract_metrics_from_page(global_opts: List[str] = None) -> Optional[FrameMetrics]:
    """Extract frame metrics from the web page using JavaScript eval"""
    # Use browser-use eval with proper escaping
    output = run_browser_use_cmd([
        "eval",
        "JSON.stringify(window.__clientFrameMetrics || {})"
    ], global_opts)

    try:
        # Parse JSON from output - browser-use returns: result: JSON_STRING
        if "result:" in output:
            # Extract the JSON part after "result:"
            json_str = output.split("result:", 1)[1].strip()
            # Handle case where result is on the next line
            if json_str.startswith("None") or not json_str.startswith("{"):
                return None

            # Try to find and parse JSON object
            match = re.search(r'\{[^{}]*(?:\{[^{}]*\}[^{}]*)*\}', json_str)
            if match:
                data = json.loads(match.group())
                if data and "rendered" in data:
                    return FrameMetrics(
                        rendered=data.get("rendered", 0),
                        received=data.get("received", 0),
                        queue_depth=data.get("queueDepth", 0),
                        unique_rendered=data.get("uniqueRendered", 0),
                        frame_hashes=data.get("frameHashes", {})
                    )
    except (json.JSONDecodeError, AttributeError, ValueError):
        pass

    return None


def take_screenshot(filename: str = "client-screenshot.png", global_opts: List[str] = None):
    """Take a screenshot of the current browser state"""
    return run_browser_use_cmd(["screenshot", filename], global_opts)


def debug_ascii_frames(
    duration: int = 30,
    interval: float = 1.0,
    server_url: str = "ws://localhost:27226",
    headed: bool = False
):
    """
    Main debugging function that monitors frame transmission

    Args:
        duration: How long to monitor in seconds (default 30)
        interval: Metrics collection interval in seconds (default 1.0)
        server_url: Server WebSocket URL (default ws://localhost:27226)
        headed: Show browser window (default False for headless)
    """
    print("🚀 Starting ASCII Chat Frame Debugging Session")
    print(f"📊 Will collect metrics for {duration} seconds every {interval}s")
    print(f"🔗 Target Server: {server_url}")
    print()

    # Build initial navigation command with global options
    # Use both --headed and --browser chromium for visible window
    global_opts = ["--headed", "--browser", "chromium"] if headed else []
    print("🌐 Opening browser and navigating to http://localhost:3000/client...")

    nav_cmd = ["open", "http://localhost:3000/client"]
    nav_output = run_browser_use_cmd(nav_cmd, global_opts)

    if "TIMEOUT" in nav_output or not nav_output:
        print("❌ Failed to open browser. Make sure:")
        print("   1. npm run dev is running (web server on :3000)")
        print("   2. browser-use is installed: pip install browser-use")
        print("   3. ANTHROPIC_API_KEY environment variable is set")
        return

    # Give page time to load and connect
    print("⏳ Waiting for client to load and connect...")
    time.sleep(3)

    # Inject fake webcam to generate test frames
    print("🎬 Injecting fake webcam with animated test pattern...")
    fake_webcam_code = """
    // Create a fake MediaStream with animated canvas
    const canvas = document.createElement('canvas');
    canvas.width = 640;
    canvas.height = 480;
    const ctx = canvas.getContext('2d');

    let frameCount = 0;
    setInterval(() => {
        frameCount++;
        // Create animated test pattern
        const hue = (frameCount % 360);
        ctx.fillStyle = `hsl(${hue}, 100%, 50%)`;
        ctx.fillRect(0, 0, canvas.width, canvas.height);

        // Add some moving geometry
        ctx.fillStyle = 'white';
        const x = (frameCount * 2) % canvas.width;
        ctx.fillRect(x, 200, 50, 50);

        // Add text
        ctx.fillStyle = 'black';
        ctx.font = '20px Arial';
        ctx.fillText(`Frame ${frameCount}`, 10, 30);
    }, 1000/30);

    // Override getUserMedia to return canvas as fake webcam
    const originalGetUserMedia = navigator.mediaDevices.getUserMedia;
    navigator.mediaDevices.getUserMedia = async function(constraints) {
        const stream = canvas.captureStream(30);
        console.log('[FakeWebcam] Stream created with 30 FPS');
        return stream;
    };
    console.log('[FakeWebcam] Injected - getUserMedia overridden');
    """

    inject_result = run_browser_use_cmd(["eval", fake_webcam_code], global_opts)
    if "FakeWebcam" in inject_result or "error" not in inject_result.lower():
        print("✅ Fake webcam injected successfully")
    else:
        print("⚠️  Fake webcam injection returned:", inject_result[:100])

    # Wait a moment for injection to take effect
    time.sleep(1)

    # Wait for permission dialog and try to click Allow
    print("🎥 Waiting for permission dialog...")
    time.sleep(1)

    # Try to click "Allow" on permission dialog
    allow_perms_code = """
    // Look for Allow button in permission dialog
    const buttons = Array.from(document.querySelectorAll('button')).filter(b =>
        b.textContent.toLowerCase().includes('allow') ||
        b.textContent.toLowerCase().includes('yes')
    );
    if (buttons.length > 0) {
        buttons[0].click();
        console.log('[Permissions] Clicked Allow button');
    } else {
        console.log('[Permissions] No Allow button found - might be browser native dialog');
    }
    """
    run_browser_use_cmd(["eval", allow_perms_code], global_opts)
    time.sleep(2)

    # Try to auto-click the "Start Webcam" button
    print("🎥 Attempting to start webcam...")
    auto_start_code = """
    const buttons = Array.from(document.querySelectorAll('button')).filter(b =>
        b.textContent.toLowerCase().includes('webcam') ||
        b.textContent.toLowerCase().includes('start')
    );
    if (buttons.length > 0) {
        buttons[0].click();
        console.log('[AutoStart] Clicked webcam button');
    } else {
        console.log('[AutoStart] No webcam button found');
    }
    """
    run_browser_use_cmd(["eval", auto_start_code], global_opts)
    time.sleep(2)  # Wait for webcam to start

    # Start monitoring loop
    stats: List[Dict] = []
    start_time = time.time()
    last_metrics: Optional[FrameMetrics] = None
    ascii_frame_count = 0
    unique_frames_seen = set()

    print("📈 Collecting metrics...\n")

    while time.time() - start_time < duration:
        try:
            metrics = extract_metrics_from_page(global_opts)

            if metrics:
                # Track changes
                new_frames_received = (
                    metrics.received - (last_metrics.received if last_metrics else 0)
                )
                new_frames_rendered = (
                    metrics.rendered - (last_metrics.rendered if last_metrics else 0)
                )

                if new_frames_received > 0:
                    ascii_frame_count += new_frames_received

                # Track unique frames
                if metrics.frame_hashes:
                    unique_frames_seen.update(metrics.frame_hashes.keys())

                # Display progress
                elapsed = time.time() - start_time
                line = (
                    f"\r⏱️  {elapsed:5.1f}s | "
                    f"Received: {metrics.received:4d} | "
                    f"Rendered: {metrics.rendered:4d} | "
                    f"Queue: {metrics.queue_depth:2d} | "
                    f"Unique: {metrics.unique_rendered:3d}"
                )
                print(line, end="", flush=True)

                last_metrics = metrics
                stats.append({
                    "timestamp": time.time(),
                    "metrics": metrics,
                    "new_received": new_frames_received,
                    "new_rendered": new_frames_rendered,
                })
            else:
                print(
                    "\r⚠️  Could not read metrics from page (page may not be ready)",
                    end="",
                    flush=True
                )

        except Exception as e:
            print(f"\n⚠️  Error reading metrics: {e}")

        time.sleep(interval)

    # Print summary report
    print("\n\n📋 ════════════════════════════════════════════════")
    print("                  DEBUG SUMMARY")
    print("════════════════════════════════════════════════\n")

    if not stats or not last_metrics:
        print("❌ No metrics collected.")
        print("\nPossible issues:")
        print("  • Web server not running (npm run dev)")
        print("  • ASCII-chat server not running (./build/bin/ascii-chat server)")
        print("  • Client page failed to load")
        print("  • Connection failed\n")
        return

    first_metrics = stats[0]["metrics"]
    final_metrics = last_metrics

    print("📊 Frame Statistics:")
    print(f"  • Frames Received: {final_metrics.received}")
    print(f"    (from {first_metrics.received} at start, gained {final_metrics.received - first_metrics.received})")
    print(f"  • Frames Rendered: {final_metrics.rendered}")
    print(f"    (from {first_metrics.rendered} at start, gained {final_metrics.rendered - first_metrics.rendered})")
    print(f"  • Unique Frames: {final_metrics.unique_rendered}")
    print(f"  • Final Queue Depth: {final_metrics.queue_depth}")

    # Calculate FPS if we have rendering data
    rendered_delta = final_metrics.rendered - first_metrics.rendered
    if rendered_delta > 0:
        time_delta = (stats[-1]["timestamp"] - stats[0]["timestamp"])
        if time_delta > 0:
            fps = rendered_delta / time_delta
            print(f"\n📹 Rendering Performance:")
            print(f"  • FPS: {fps:.1f} frames per second")
            print(f"  • Frame Time: ~{1000/fps:.1f}ms per frame")

    # Network frame arrival rate
    received_delta = final_metrics.received - first_metrics.received
    if received_delta > 0:
        time_delta = (stats[-1]["timestamp"] - stats[0]["timestamp"])
        if time_delta > 0:
            arrival_fps = received_delta / time_delta
            print(f"\n🌐 Network Performance:")
            print(f"  • Frame Arrival Rate: {arrival_fps:.1f} frames per second")
            print(f"  • Frame Interval: ~{1000/arrival_fps:.1f}ms between frames")

    # Frame variety analysis
    print(f"\n🎨 Frame Variety:")
    print(f"  • Unique Frame Hashes Seen: {len(unique_frames_seen)}")
    if final_metrics.frame_hashes:
        print(f"  • Top Frame Distribution:")
        sorted_hashes = sorted(
            final_metrics.frame_hashes.items(),
            key=lambda x: x[1],
            reverse=True
        )[:5]
        for idx, (hash_val, count) in enumerate(sorted_hashes, 1):
            percentage = (count / final_metrics.rendered * 100) if final_metrics.rendered > 0 else 0
            print(f"    {idx}. Hash {hash_val}: {count} frames ({percentage:.1f}%)")

    # Health check
    print(f"\n✅ Health Check:")

    if final_metrics.received > 0:
        print(f"  ✓ ASCII frames ARE being received from server")
        status_received = True
    else:
        print(f"  ✗ NO ASCII frames received - check server connection")
        status_received = False

    if final_metrics.rendered > 0:
        print(f"  ✓ Frames ARE being rendered to xterm.js")
        status_rendered = True
    else:
        print(f"  ✗ NO frames rendered to xterm.js - check renderer setup")
        status_rendered = False

    if final_metrics.unique_rendered > 1:
        print(f"  ✓ Multiple unique frames detected (good content variation)")
    elif final_metrics.unique_rendered == 1:
        print(f"  ⚠️  Only 1 unique frame - possible static/freeze issue")
    else:
        print(f"  ✗ No unique frames rendered")

    if final_metrics.queue_depth < 5:
        print(f"  ✓ Frame queue depth healthy ({final_metrics.queue_depth})")
    else:
        print(f"  ⚠️  Frame queue building up (depth: {final_metrics.queue_depth})")

    # Overall verdict
    print(f"\n📝 Overall Assessment:")
    if status_received and status_rendered and final_metrics.unique_rendered > 1:
        print("  ✅ GOOD - Frames flowing and rendering properly")
    elif status_received and status_rendered:
        print("  ⚠️  PARTIAL - Frames flowing but limited variety")
    elif status_received and not status_rendered:
        print("  ⚠️  ISSUE - Frames received but not rendered")
    elif not status_received:
        print("  ❌ BROKEN - No frames being received from server")

    print("\n════════════════════════════════════════════════")
    print("💡 Debug Tips:")
    print("  • Check browser console for [Client] logs")
    print("  • Monitor server: ./build/bin/ascii-chat --log-level debug server --grep 'frame|packet'")
    print("  • Monitor client: browser DevTools Network tab (WS messages)")
    print("════════════════════════════════════════════════\n")

    # Take final screenshot
    print("📸 Taking final screenshot...")
    screenshot_output = take_screenshot("client-debug-final.png", global_opts)
    print("Screenshot saved to: client-debug-final.png\n")

    return stats


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Debug ASCII Chat frame transmission",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python web/scripts/debug-frames.py
  python web/scripts/debug-frames.py --duration 60 --interval 2
  python web/scripts/debug-frames.py --headed  # Show browser window
        """
    )
    parser.add_argument(
        "--duration",
        type=int,
        default=30,
        help="Duration to monitor in seconds (default: 30)"
    )
    parser.add_argument(
        "--interval",
        type=float,
        default=1.0,
        help="Metrics collection interval in seconds (default: 1.0)"
    )
    parser.add_argument(
        "--server",
        default="ws://localhost:27226",
        help="Server WebSocket URL (default: ws://localhost:27226)"
    )
    parser.add_argument(
        "--headed",
        action="store_true",
        help="Show browser window (default: headless)"
    )

    args = parser.parse_args()

    debug_ascii_frames(
        duration=args.duration,
        interval=args.interval,
        server_url=args.server,
        headed=args.headed
    )
