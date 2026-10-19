"""Generate real audio/video media and exercise source replacement and cancellation."""
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="ascii-chat-media-") as directory:
    media = Path(directory) / "movie with spaces.mp4"
    subprocess.run([
        "ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i", "color=c=red:s=64x48:r=30",
        "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000", "-t", "1",
        "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac", str(media),
    ], check=True, timeout=30)
    subprocess.run([sys.argv[1], str(media)], check=True, timeout=60)
