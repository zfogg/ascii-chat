"""Download a moving-face test clip and compose repeatable acquisition/loss scenarios."""
import argparse
import hashlib
import pathlib
import subprocess
import urllib.request

parser = argparse.ArgumentParser()
parser.add_argument("--output", default="artifacts/face-follow")
args = parser.parse_args()
output = pathlib.Path(args.output)
output.mkdir(parents=True, exist_ok=True)
source = output / "faceocc2.webm"
url = "https://raw.githubusercontent.com/opencv/opencv_extra/cb09893cc0d1147787b254597cb78c5ae854185a/testdata/cv/tracking/faceocc2/data/faceocc2.webm"
if not source.exists():
    urllib.request.urlretrieve(url, source)
if hashlib.sha256(source.read_bytes()).hexdigest() != "5ffedeca4fb3acfd65e6f6aa71f016f748628727be1e24914fde2198b42089c5":
    raise SystemExit("Face Follow source checksum mismatch: remove the cached clip and retry.")
filters = (
    "[0:v]trim=duration=4,loop=loop=-1:size=100:start=0,setpts=N/(25*TB),split=2[a][b];[a]scale=240:180[face];[b]scale=280:210[other];"
    "[1:v][face]overlay=x='75+55*sin(t)':y=80:enable='lt(t,9)+gte(t,13)'[one];"
    "[one][other]overlay=x=350:y=75:enable='between(t,4,9)'[out]"
)
subprocess.run([
    "ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(source),
    "-f", "lavfi", "-i", "color=c=0x242830:s=640x360:r=25:d=18",
    "-filter_complex", filters, "-map", "[out]", "-t", "18", "-an",
    "-c:v", "libx264", "-pix_fmt", "yuv420p", str(output / "scenarios.mp4"),
], check=True)
subprocess.run([
    "ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(source),
    "-t", "4", "-vf", "scale=640:480", "-pix_fmt", "yuv420p", str(output / "webcam.y4m"),
], check=True)
still = output / "seated-face.png"
subprocess.run([
    "ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-ss", "1", "-i", str(source),
    "-frames:v", "1", str(still),
], check=True)
subprocess.run([
    "ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-loop", "1", "-i", str(still),
    "-f", "lavfi", "-i", "color=c=0x242830:s=640x360:r=25:d=18",
    "-filter_complex",
    "[1:v][0:v]overlay=x='140+if(between(t,3,9),25*sin(2*t),0)+if(between(t,10,10.35),55,0)+if(gte(t,13),65,0)':y='60+if(between(t,3,9),12*sin(3*t),0)'[out]",
    "-map", "[out]", "-t", "18", "-an", "-c:v", "libx264", "-pix_fmt", "yuv420p",
    str(output / "seated.mp4"),
], check=True)
print(f"Source: {url}\nFixture: {output / 'scenarios.mp4'}\nCamera: {output / 'webcam.y4m'}")
