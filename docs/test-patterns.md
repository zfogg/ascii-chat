# Shared native and browser test patterns

`--test-pattern` and `--test-pattern 0` select the animated gradient and translucent square. `--test-pattern 1` selects rainbow stripes and a wrapping white circle. Both also accept `=0`/`=1`. Omitting the flag leaves normal source selection unchanged; `0` means the first animation, not disabled. The option is available in mirror, client, and discovery modes.

```sh
ascii-chat mirror --test-pattern
ascii-chat mirror --test-pattern 1 --color-mode 256
ascii-chat client localhost --test-pattern=0
```

`WEBCAM_DISABLED=1` remains a boolean enable switch; with default configuration it enables pattern zero. Existing boolean `[webcam] test_pattern = true/false` configuration remains supported; integer `0` or `1` enables the selected animation. CLI selection overrides configuration/environment values.

The browser maps `?test` to pattern zero and `?test2` to pattern one. `?test2&testCadence` explicitly enables the full-height Gray-code diagnostic overlay; ordinary `?test2` now shows its stripes and circle.

## Implementation

`lib/video/anim/test_pattern.c` owns the RGB animations, a high-contrast pixel-lettered `test` label, and optional reusable RGBA output. Each media source/canvas has an independent renderer and reusable image buffers. Its public API takes elapsed milliseconds, making animation speed independent of capture FPS and allowing deterministic comparisons. Native sources use a monotonic clock. Geometry adapts below the former Canvas minimum shape sizes so the minimum 20x10 terminal can still show the shapes and colors.

`src/web/common/test_pattern.c` exposes this same implementation to both WASM modules. TypeScript only copies/uploads the generated RGBA frame and manages source lifetime. The mirror consumes the returned frame without reading the canvas back.

The existing terminal palette helpers are shared by the normal render pipeline. `rgb_to_256color` now compares the nearest nonuniform xterm cube entry with the nearest grayscale entry, including exact black and white. `get_256color_rgb` supplies the inverse palette lookup; the existing 16-color helpers supply the other direction. Palette expansion uses the standard reference colors (the first 16 actual terminal colors may be themed).

## Recorded verification (2026-10-09)

The C/WASM output was compared against the former Canvas implementation before removing the JavaScript reference and dedicated browser tests. The retained screenshots document that verification:

![Canvas reference and C/WASM gradient](test-pattern-evidence/pattern-0-paired.png)
![Canvas reference and C/WASM stripes](test-pattern-evidence/pattern-1-paired.png)

These earlier comparisons used bundled DejaVu Sans Mono labels. The current renderer instead draws a larger lowercase `test` in integer-sized pixel strokes on a black backing, near the top. This keeps the word readable after ASCII conversion; labels are omitted when the source is too small to fit them. Both native and browser sources use the same label.

Both visible patterns sustained approximately 60 materially changing ASCII frames per second for 10 seconds in a 1920x1080 browser viewport. Application screenshots show the warmed-up counter: [mirror zero](test-pattern-evidence/test-mirror.png), [mirror one](test-pattern-evidence/test2-mirror.png).

Native source images: [pattern zero](test-pattern-evidence/native-pattern-0.png), [pattern one](test-pattern-evidence/native-pattern-1.png). Native CLI and shared-library checks cover selectors/defaults, 30 snapshots, legacy environment inputs, palette mapping, independent sources, resize, and invalid input. The config loader was separately verified with `true`, `false`, `0`, and `1` from both initial enable states (eight cases); the native unit regression calls the loader directly. The previous CLI check only checked exit status and did not establish that the requested config file was loaded. [Recorded results](test-pattern-evidence/native-results.json).

## Native verification

```sh
cmake --preset default -B build
cmake --build build --target ascii-chat
python tests/platform/test_pattern.py --binary build/bin/ascii-chat.exe --library build/bin/asciichat.dll
```

Criterion tests require a supported platform; Windows verification uses the actual shared library. Native PNG encoder initialization fails in this environment, so the native evidence images were captured directly from the library.
