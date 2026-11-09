# Experimental glyph matching

`--renderer=glyph` enables experimental glyph matching in mirror, client,
discovery, and MCP rendering. It samples an 8×8 pixel patch per terminal cell,
averages the patch to 4×4, and matches both luminance and local shape to glyph
masks. It colors the selected character using its covered pixels, boosting dim
colors to keep the ink visible. With `--render-mode=background`, the background
receives a dark tint derived from the cell's average color. Terminal color
quantization happens after glyph selection. The default luminance renderer is
unchanged.

The design was informed by [Chafa](https://github.com/hpjansson/chafa), inspected
at `c897fce60cc4e947a254770ea1c2e320d58e7dce`. Chafa uses 8×8 symbol
coverage, selects candidates (or evaluates the whole set), estimates colors and
scores the reconstructed cell. This prototype is an independent, smaller
implementation; it does not copy Chafa code or glyph definitions. The generated
font masks carry the license in `lib/video/ascii/DEJAVU-LICENSE`.

ASCII masks are generated from DejaVu Sans Mono 2.37; block-element masks use
their defined geometry. Other fonts and terminal rasterization can change the
result. The default palette contains the 95 printable ASCII characters. An
explicit palette restricts candidates to its unique characters and may include
Unicode block elements U+2580–U+2593. Palettes containing other characters are
rejected with an error. Half-block mode and viewports larger than 480×270 cells
use the luminance renderer. Optional `--stabilize` reduces frame-to-frame
flicker in foreground, background, and half-block modes.

## Visual comparison

These historical side-by-side captures predate the current glyph scoring and
Unicode block support. They were made with Windows x64 debug builds at 80×32 cells,
truecolor, 8 fps. The emitted terminal frames were rasterized with Pillow and
the same DejaVu font used for the atlas. The app-produced GIFs repeated their
first image in this run, so they were not used for the comparisons. The left
side uses the current renderer and the right side uses `--renderer=glyph`. The
first three animations are roughly two seconds; their PNGs show frame 7. The
website demo still shows frame 49. The two renderings of each test
pattern were captured in separate runs and can differ slightly in animation
phase. The FFmpeg clip is deterministic and was generated as shown below.

The comparisons cover test patterns 0 and 1, FFmpeg `testsrc`, and a silent
30-second excerpt of the website demo from 0:45 to 1:15. The source of the
latter is `web/www/public/assets/demo-video-white-knuckles.webm`. Each side was
captured separately at 8 fps and combined by frame index, so small differences
in capture timing remain. The generated media is kept outside the repository.

The historical comparison used the standard 23-character ramp for luminance
and 95 printable glyphs for glyph matching.

Matching sometimes introduces directional glyphs at shape boundaries, while
flat bands gain conspicuous repeated characters. The examples do not establish an overall quality improvement; font, image content,
and motion need further evaluation.

```sh
ffmpeg -f lavfi -i 'testsrc=duration=3:size=640x480:rate=8' -c:v mpeg4 -q:v 2 demo.mp4
ascii-chat mirror --test-pattern=0 --snapshot --snapshot-delay=2 --audio=false --fps=8 \
  --width=80 --height=32 --color-mode=truecolor --render-file=baseline.gif > pattern0-baseline.log 2>&1
ascii-chat mirror --test-pattern=0 --snapshot --snapshot-delay=2 --audio=false --fps=8 \
  --width=80 --height=32 --color-mode=truecolor --renderer=glyph --render-file=glyph.gif > pattern0-glyph.log 2>&1
# Repeat with --test-pattern=1 and --file=demo.mp4, naming the logs
# pattern1-{baseline,glyph}.log and testsrc-{baseline,glyph}.log.
uv run --project tests/ascii_chat_tests --frozen python scripts/render_glyph_comparisons.py CAPTURE_DIR OUTPUT_DIR DejaVuSansMono.ttf

# Extract a 30-second action segment and capture it twice. Use the same options
# for both sides except --renderer=glyph.
ffmpeg -ss 45 -i web/www/public/assets/demo-video-white-knuckles.webm -t 30 \
  -an -c:v libx264 -crf 18 demo-action.mp4
ascii-chat --quiet --no-check-update mirror \
  --file=demo-action.mp4 \
  --snapshot --snapshot-delay=30 --audio=false --fps=8 --width=80 --height=32 \
  --color-mode=truecolor > baseline.log 2>&1
ascii-chat --quiet --no-check-update mirror \
  --file=demo-action.mp4 \
  --snapshot --snapshot-delay=30 --audio=false --fps=8 --width=80 --height=32 \
  --color-mode=truecolor --renderer=glyph > glyph.log 2>&1
uv run --project tests/ascii_chat_tests --frozen python scripts/render_ansi_video.py baseline.log glyph.log website-demo.mp4 DejaVuSansMono.ttf
```

## Renderer-only timing

On the development machine, a Linux x86-64 Docker run using Clang 23, Release
`-O3`, and AVX2 rendered an 80×32 truecolor gradient in 6.7 ms per frame over
200 measured frames after five warmups. A separate run of the previous glyph
matcher on the same input measured 1.1 ms per frame. These are renderer-only
measurements under shared load, not an end-to-end video throughput guarantee;
the color fitting adds substantial work. A smaller selected palette evaluates
fewer candidates, and truecolor scoring skips remaining channels once a
candidate cannot beat the best score.

### Historical renderer-only timing

These measurements predate reconstructed-pixel color fitting; they are not a
performance measurement of the current implementation.

On a Ryzen 7 5800X, WSL2 x86-64 Linux, Clang 14, Release `-O3`, AVX2 dispatch,
80×32 truecolor cells, three runs of 200 measured iterations after five warmups:

| Renderer    | Mean time per frame across each run |
| ----------- | ----------------------------------- |
| Current     | 0.016, 0.020, 0.015 ms              |
| Glyph match | 2.293, 2.327, 2.332 ms              |

The test uses the first frame of the FFmpeg clip, cropped to the viewport and
nearest-neighbor sampled to 640×256. The baseline takes one sample per cell;
the matcher receives the 8×8 patch. Both calls allocate and emit text. Timings
exclude image fitting/resizing, capture, encoding, terminal writes, and the rest
of the application. The matching stage is roughly 145× slower using the median values in this small
benchmark, though still below a 16.7 ms frame budget by itself. This is not an
end-to-end throughput guarantee; concurrent load and other platforms can differ.

```sh
ffmpeg -v error -i demo.mp4 -vf 'crop=240:480:200:0,scale=640:256:flags=neighbor' \
  -frames:v 1 -pix_fmt rgb24 -f rawvideo benchmark.rgb
cmake -S tests/simd -B /tmp/glyph-simd -G Ninja -DTEST_EMISSION=ON \
  -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/glyph-simd --target emission-test glyph-benchmark
ctest --test-dir /tmp/glyph-simd -R simd-emission --output-on-failure
/tmp/glyph-simd/glyph-benchmark benchmark.rgb
```
