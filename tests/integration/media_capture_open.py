"""Generate real audio/video media and exercise source replacement and cancellation."""
from pathlib import Path
import subprocess
import shutil
import sys
import tempfile

if shutil.which("ffmpeg") is None:
    print("ffmpeg not found; skipping")
    sys.exit(77)
encoders = subprocess.run(["ffmpeg", "-hide_banner", "-encoders"], capture_output=True, text=True, timeout=10)
if not all(name in encoders.stdout for name in ("libx264", "aac")):
    print("ffmpeg requires libx264 and aac encoders; skipping")
    sys.exit(77)

with tempfile.TemporaryDirectory(prefix="ascii-chat-media-") as directory:
    media = Path(directory) / "movie with spaces.mp4"
    subprocess.run([
        "ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i", "color=c=red:s=64x48:r=30",
        "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000", "-t", "1",
        "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac", str(media),
    ], check=True, timeout=30)
    subprocess.run([sys.argv[1], str(media)], check=True, timeout=60)
