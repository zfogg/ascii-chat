# Generic Video Flip Options Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Convert webcam-specific `--webcam-flip` to generic display options `--flip-x` and `--flip-y` that work on all video sources (webcam, yt-dlp, ffmpeg).

**Architecture:** Replace single `webcam_flip` boolean with two separate `flip_x` and `flip_y` booleans in the display options registry. Move flip application from webcam capture stage to generic frame rendering stage so it applies uniformly to all video sources. Persist settings across media source changes and allow toggling via keyboard handler.

**Tech Stack:** C, CMake, libsodium, FFmpeg, yt-dlp integration

---

### Task 1: Update options_t struct and defaults

**Files:**
- Modify: `include/ascii-chat/options/options.h:1-100`

**Step 1: Remove webcam_flip field from struct**

In `options_t struct`, find and remove:
```c
bool webcam_flip;                ///< Flip webcam image horizontally
```

**Step 2: Add flip_x and flip_y fields to struct**

After removing `webcam_flip`, add:
```c
bool flip_x;                     ///< Flip video horizontally (X-axis)
bool flip_y;                     ///< Flip video vertically (Y-axis)
```

**Step 3: Remove old default constant**

Remove:
```c
#define OPT_WEBCAM_FLIP_DEFAULT false
```

**Step 4: Add new default constants**

Add:
```c
#define OPT_FLIP_X_DEFAULT false
#define OPT_FLIP_Y_DEFAULT false
```

**Step 5: Remove old default variable declaration**

Remove:
```c
static const bool default_webcam_flip_value = OPT_WEBCAM_FLIP_DEFAULT;
```

**Step 6: Add new default variable declarations**

Add:
```c
static const bool default_flip_x_value = OPT_FLIP_X_DEFAULT;
static const bool default_flip_y_value = OPT_FLIP_Y_DEFAULT;
```

**Step 7: Commit**

```bash
git add include/ascii-chat/options/options.h
git commit -m "refactor(options): Replace webcam_flip with flip_x and flip_y fields"
```

---

### Task 2: Update option registry from webcam to display

**Files:**
- Modify: `lib/options/registry/webcam.c:1-50` (remove webcam-flip entry)
- Create: `lib/options/registry/display.c` (new file if doesn't exist, add flip options)

**Step 1: Check if display.c exists**

Run: `ls -la lib/options/registry/display.c`

**Step 2: If display.c doesn't exist, create it**

Create new file `lib/options/registry/display.c`:
```c
/**
 * @file display.c
 * @brief Display rendering options
 * @ingroup options
 */

#include <ascii-chat/options/registry/common.h>

const registry_entry_t g_display_entries[] = {
    {"flip-x",
     '\0',
     OPTION_TYPE_BOOL,
     offsetof(options_t, flip_x),
     &default_flip_x_value,
     sizeof(bool),
     "Flip video horizontally (X-axis). Works with webcam, files, and streams.",
     "DISPLAY",
     NULL,
     false,
     "ASCII_CHAT_FLIP_X",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR,
     {0},
     NULL},
    {"flip-y",
     '\0',
     OPTION_TYPE_BOOL,
     offsetof(options_t, flip_y),
     &default_flip_y_value,
     sizeof(bool),
     "Flip video vertically (Y-axis). Works with webcam, files, and streams.",
     "DISPLAY",
     NULL,
     false,
     "ASCII_CHAT_FLIP_Y",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR,
     {0},
     NULL},

    REGISTRY_TERMINATOR()};
```

**Step 3: If display.c exists, add flip options to it**

Find the `g_display_entries` array and add the two flip entries above.

**Step 4: Remove webcam-flip from webcam.c**

In `lib/options/registry/webcam.c`, find and remove the entire `{"webcam-flip", ...}` entry (approximately 15 lines).

**Step 5: Commit**

```bash
git add lib/options/registry/webcam.c lib/options/registry/display.c
git commit -m "feat(options): Move flip options from webcam to display registry as flip-x and flip-y"
```

---

### Task 3: Update options initialization

**Files:**
- Modify: `lib/options/options.c:1-100`

**Step 1: Update options_t_new() initializer**

Find `options_t_new()` function and replace:
```c
.webcam_flip = OPT_WEBCAM_FLIP_DEFAULT,
```

With:
```c
.flip_x = OPT_FLIP_X_DEFAULT,
.flip_y = OPT_FLIP_Y_DEFAULT,
```

**Step 2: Update config file parsing preservation**

Find references to `saved_webcam_flip` (there are multiple places where webcam_flip is preserved during config parsing). Replace each occurrence of `webcam_flip` with both `flip_x` and `flip_y`.

For example, replace:
```c
bool saved_webcam_flip = opts.webcam_flip;
// ... code ...
opts.webcam_flip = saved_webcam_flip;
```

With:
```c
bool saved_flip_x = opts.flip_x;
bool saved_flip_y = opts.flip_y;
// ... code ...
opts.flip_x = saved_flip_x;
opts.flip_y = saved_flip_y;
```

Do this for all occurrences in config loading, defaults application, and parsing stages.

**Step 3: Commit**

```bash
git add lib/options/options.c
git commit -m "refactor(options): Update initialization to use flip_x and flip_y"
```

---

### Task 4: Update RCU (Runtime Configuration Update) handling

**Files:**
- Modify: `lib/options/rcu.c:1-200`

**Step 1: Replace webcam_flip in default initializer**

Find where `options_rcu_defaults_t` is initialized and replace:
```c
.webcam_flip = OPT_WEBCAM_FLIP_DEFAULT,
```

With:
```c
.flip_x = OPT_FLIP_X_DEFAULT,
.flip_y = OPT_FLIP_Y_DEFAULT,
```

**Step 2: Replace webcam_flip in field assignment**

Find `else if (strcmp(ctx->field_name, "webcam_flip") == 0)` and replace with:
```c
else if (strcmp(ctx->field_name, "flip_x") == 0)
  opts->flip_x = ctx->value;
else if (strcmp(ctx->field_name, "flip_y") == 0)
  opts->flip_y = ctx->value;
```

**Step 3: Replace webcam_flip in validation checks**

Find the string validation that includes `"webcam_flip"` and replace with `"flip_x"` and `"flip_y"` entries.

**Step 4: Commit**

```bash
git add lib/options/rcu.c
git commit -m "refactor(rcu): Update field names from webcam_flip to flip_x and flip_y"
```

---

### Task 5: Update test helpers

**Files:**
- Modify: `include/ascii-chat/tests/common.h:1-50`

**Step 1: Remove old test helper**

Remove:
```c
static inline void test_set_webcam_flip(bool value) {
  options_set_bool("webcam_flip", value);
}
```

**Step 2: Add new test helpers**

Add:
```c
static inline void test_set_flip_x(bool value) {
  options_set_bool("flip_x", value);
}

static inline void test_set_flip_y(bool value) {
  options_set_bool("flip_y", value);
}
```

**Step 3: Commit**

```bash
git add include/ascii-chat/tests/common.h
git commit -m "refactor(tests): Add test helpers for flip_x and flip_y"
```

---

### Task 6: Update webcam capture code

**Files:**
- Modify: `lib/video/webcam/webcam.c:100-150`

**Step 1: Remove flip logic from webcam capture**

Find the two locations where `if (GET_OPTION(webcam_flip))` appears and remove both flip operations. These are in the webcam initialization and frame capture paths.

Search for exact patterns:
- `if (GET_OPTION(webcam_flip)) {` - remove entire if block
- Line with `if (GET_OPTION(webcam_flip) && frame->w > 1)` - remove entire if block

**Step 2: Verify no flip logic remains in webcam.c**

Run: `grep -n "flip" lib/video/webcam/webcam.c`

Expected: Should return nothing or only comments.

**Step 3: Commit**

```bash
git add lib/video/webcam/webcam.c
git commit -m "refactor(webcam): Remove flip logic from capture stage"
```

---

### Task 7: Add flip logic to frame rendering

**Files:**
- Modify: `lib/session/display.c:1-300`

**Step 1: Find frame rendering function**

Search for where frames are rendered to display. Look for function that processes video frames before ASCII conversion.

**Step 2: Add flip logic before rendering**

In the rendering path, add a function that applies flips:
```c
/**
 * Apply flip transformations to frame
 */
static void apply_frame_flips(framebuffer_t *frame) {
  if (!frame || frame->w <= 1 || frame->h <= 1) {
    return;
  }

  bool flip_x = GET_OPTION(flip_x);
  bool flip_y = GET_OPTION(flip_y);

  if (!flip_x && !flip_y) {
    return;  // No flips requested
  }

  uint32_t *pixels = (uint32_t *)frame->data;
  size_t stride = frame->stride / sizeof(uint32_t);

  // Apply horizontal flip (X-axis)
  if (flip_x) {
    for (size_t y = 0; y < frame->h; y++) {
      uint32_t *row = &pixels[y * stride];
      for (size_t x = 0; x < frame->w / 2; x++) {
        uint32_t temp = row[x];
        row[x] = row[frame->w - 1 - x];
        row[frame->w - 1 - x] = temp;
      }
    }
  }

  // Apply vertical flip (Y-axis)
  if (flip_y) {
    for (size_t y = 0; y < frame->h / 2; y++) {
      uint32_t *top_row = &pixels[y * stride];
      uint32_t *bottom_row = &pixels[(frame->h - 1 - y) * stride];
      for (size_t x = 0; x < frame->w; x++) {
        uint32_t temp = top_row[x];
        top_row[x] = bottom_row[x];
        bottom_row[x] = temp;
      }
    }
  }
}
```

**Step 3: Call flip logic in rendering path**

Find the main rendering function (likely `display_render()` or similar) and call `apply_frame_flips(frame)` right before the frame is converted to ASCII.

**Step 4: Commit**

```bash
git add lib/session/display.c
git commit -m "feat(display): Add flip_x and flip_y rendering to frame pipeline"
```

---

### Task 8: Update keyboard handler

**Files:**
- Modify: `lib/session/keyboard_handler.c:100-200`

**Step 1: Find flip toggle in keyboard handler**

Search for `'f'` key handler that toggles `webcam_flip`.

**Step 2: Replace with flip_x toggle**

Replace:
```c
bool current_flip = (bool)GET_OPTION(webcam_flip);
options_set_bool("webcam_flip", !current_flip);
```

With:
```c
bool current_flip_x = (bool)GET_OPTION(flip_x);
options_set_bool("flip_x", !current_flip_x);
```

**Step 3: Add flip_y toggle (optional, use 'shift+f' or another key)**

If you want keyboard toggle for flip_y, add another case. For now, just support flip_x toggle with 'f' key.

**Step 4: Commit**

```bash
git add lib/session/keyboard_handler.c
git commit -m "refactor(keyboard): Update flip toggle to use flip_x option"
```

---

### Task 9: Update help screen references

**Files:**
- Modify: `lib/ui/help_screen.c:1-100`

**Step 1: Find help screen flip references**

Search: `grep -n "webcam_flip\|webcam flip" lib/ui/help_screen.c`

**Step 2: Replace in help text**

Replace any references to "webcam flip" with appropriate display flip descriptions. Update to mention both flip_x and flip_y if referenced.

**Step 3: Commit**

```bash
git add lib/ui/help_screen.c
git commit -m "docs(help): Update flip option descriptions in help screen"
```

---

### Task 10: Update config file documentation

**Files:**
- Modify: `include/ascii-chat/options/config.h:1-50`

**Step 1: Find config comments**

Search for comments mentioning `webcam_flip` in the [client] section documentation.

**Step 2: Update to reflect display options**

Replace references to `webcam_flip` with `flip_x` and `flip_y`.

**Step 3: Commit**

```bash
git add include/ascii-chat/options/config.h
git commit -m "docs(config): Update documentation for flip_x and flip_y options"
```

---

### Task 11: Update web/WASM mirror bindings

**Files:**
- Modify: `src/web/mirror.c:1-50`

**Step 1: Find mirror_set_webcam_flip and mirror_get_webcam_flip**

These are WASM binding functions. Replace:
```c
int mirror_set_webcam_flip(int enabled) {
  return options_set_bool("webcam_flip", enabled != 0) == ASCIICHAT_OK ? 0 : -1;
}

int mirror_get_webcam_flip(void) {
  return GET_OPTION(webcam_flip) ? 1 : 0;
}
```

With:
```c
int mirror_set_flip_x(int enabled) {
  return options_set_bool("flip_x", enabled != 0) == ASCIICHAT_OK ? 0 : -1;
}

int mirror_get_flip_x(void) {
  return GET_OPTION(flip_x) ? 1 : 0;
}

int mirror_set_flip_y(int enabled) {
  return options_set_bool("flip_y", enabled != 0) == ASCIICHAT_OK ? 0 : -1;
}

int mirror_get_flip_y(void) {
  return GET_OPTION(flip_y) ? 1 : 0;
}
```

**Step 2: Commit**

```bash
git add src/web/mirror.c
git commit -m "refactor(wasm): Update mirror bindings to use flip_x and flip_y"
```

---

### Task 12: Build and test

**Files:**
- Test: All tests should pass

**Step 1: Clean and configure build**

Run: `rm -rf build && cmake --preset default -B build`

Expected: Configuration succeeds

**Step 2: Build**

Run: `cmake --build build 2>&1 | tail -50`

Expected: Build succeeds with no errors

**Step 3: Run unit tests**

Run: `ctest --test-dir build --label-regex "^unit$" --output-on-failure`

Expected: All unit tests pass

**Step 4: Manually test in mirror mode**

Run: `./build/bin/ascii-chat mirror --flip-x --help | grep flip`

Expected: Shows both `--flip-x` and `--flip-y` options in help

**Step 5: Test flip behavior**

Run: `./build/bin/ascii-chat mirror --flip-x --snapshot --snapshot-delay 0 | head -10`

Expected: Webcam frame appears horizontally flipped

**Step 6: Commit**

```bash
git add -A
git commit -m "test: Verify flip_x and flip_y options work correctly"
```

---

### Task 13: Update documentation

**Files:**
- Modify: `CLAUDE.md` (if flip is mentioned in debugging section)
- Modify: `docs/plans/2026-02-15-generic-stream-resolution.md` (if needs updates)

**Step 1: Search for webcam_flip in docs**

Run: `grep -r "webcam.flip\|webcam-flip" docs/ CLAUDE.md README.md 2>/dev/null`

**Step 2: Update any references**

Replace any documentation mentions with new option names.

**Step 3: Commit**

```bash
git add docs/ CLAUDE.md README.md
git commit -m "docs: Update documentation for flip_x and flip_y options"
```

---

## Summary

This plan converts the webcam-specific `--webcam-flip` option into two generic display options (`--flip-x` and `--flip-y`) that work on all video sources. Key changes:

1. **Options struct:** Replace single `webcam_flip` with `flip_x` and `flip_y`
2. **Registry:** Move from webcam to display options with new names
3. **Logic:** Remove flip from capture stage, add to rendering stage
4. **Keyboard:** Update 'f' key to toggle flip_x
5. **WASM:** Update web mirror bindings
6. **Tests:** Verify flips work on all media sources

Total commits: 13 (one per logical chunk)
