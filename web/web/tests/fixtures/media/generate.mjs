// Synthetic fixtures only: moving test pattern plus an audible 440 Hz tone.
// Run with `node tests/fixtures/media/generate.mjs` (requires native ffmpeg).
import { execFileSync } from "node:child_process";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const directory = dirname(fileURLToPath(import.meta.url));
const run = (...args) =>
  execFileSync(
    "ffmpeg",
    ["-hide_banner", "-loglevel", "error", ...args, "-y"],
    { stdio: "inherit" },
  );
const path = (name) => join(directory, name);
run(
  "-f",
  "lavfi",
  "-i",
  "testsrc2=size=320x240:rate=15",
  "-f",
  "lavfi",
  "-i",
  "sine=frequency=440:sample_rate=48000",
  "-t",
  "3",
  "-c:v",
  "libx264",
  "-pix_fmt",
  "yuv420p",
  "-c:a",
  "aac",
  path("tone.mp4"),
);
for (const [name, options] of [
  ["pcm.mov", ["-c:v", "copy", "-c:a", "pcm_s16le"]],
  ["tone.avi", ["-c:v", "mpeg4", "-q:v", "5", "-c:a", "libmp3lame"]],
  ["mjpeg.avi", ["-c:v", "mjpeg", "-q:v", "5", "-c:a", "pcm_s16le"]],
  ["tone.webm", ["-c:v", "libvpx", "-b:v", "200k", "-c:a", "libopus"]],
  [
    "tone.mpg",
    ["-c:v", "mpeg2video", "-q:v", "5", "-c:a", "mp2", "-f", "mpeg"],
  ],
  ["tone.wmv", ["-c:v", "wmv2", "-c:a", "wmav2"]],
  [
    "hevc.mkv",
    [
      "-c:v",
      "libx265",
      "-preset",
      "ultrafast",
      "-x265-params",
      "log-level=error",
      "-c:a",
      "aac",
    ],
  ],
  ["loop.gif", ["-t", "1", "-vf", "fps=5,scale=160:-1"]],
  ["still.png", ["-frames:v", "1"]],
  ["still.jpg", ["-frames:v", "1"]],
])
  run("-i", path("tone.mp4"), ...options, path(name));
