# Keyboard input

Native keyboard input has four boundaries:

- `lib/platform/{posix,windows}/keyboard.c` owns terminal/console access and restoration.
- `lib/platform/keyboard_stream.c` frames terminal escape sequences and replies.
  `keyboard_text.c` buffers fragmented text and delegates UTF-8 decoding, scalar
  validation, and encoding to utf8proc. Windows UTF-16 surrogate pairs become the
  same normalized keys; console repeats apply to complete characters.
- `lib/tui/input_events.c` collects bracketed paste and queues decoded events in
  order. The media screen supplies the policy for recognizing dropped path/URL
  bursts. Paste bytes are validated before insertion and never execute shortcuts.
- `lib/ui/text_edit.c` edits caller-owned UTF-8 storage without terminal I/O or
  rendering. Prompts and log search share this implementation; callers decide
  when to submit, cancel, mask, or insert newlines. `lib/tui/input.c` retains screen
  ownership and modal routing, and `shortcuts.c` owns shortcut aliases/help.

ASCII/control keys retain their codes. Non-ASCII text uses
`KEY_UNICODE_BASE + scalar`, avoiding collisions with special-key codes. The
browser SessionUi adapter uses the same representation, including supplementary
characters. Bracketed POSIX paste payloads remain bytes until whole-paste validation.
Malformed and truncated typed sequences cannot mutate a field; new starters or
special keys discard incomplete text. Buffer exhaustion rejects a whole inserted
character or paste without truncating it.

Editing operates on Unicode code points. Combining marks and emoji joiners remain
valid UTF-8 but are separate editing units; this is not grapheme-cluster editing.

## Validation

The normal Criterion suites exercise the production library with the configured
sanitizers: platform keyboard text/editing, UI text editing/event collection,
terminal replies, log search, and UTF-8 utilities. The password PTY regression
also types and deletes accented characters, CJK, and supplementary emoji.

On a configured Linux/macOS Clang build with Criterion and pkg-config, run:

```sh
python tests/scripts/keyboard_text_coverage.py --build build
```

This runs the same exhaustive Criterion tests for every non-NUL Unicode scalar,
malformed UTF-8, fragmented input, and UTF-16 recovery. It requires **100% line and
branch coverage of `lib/platform/keyboard_text.c`** and writes its counters, gcov
listing, and summary under the sibling `build-keyboard-text-coverage/` directory. Ubuntu Debug CI enforces
this check. The focused measurement excludes sanitizer-generated branches and
vendored utf8proc internals; the ordinary integrated tests retain sanitizers.
This percentage does not describe all Unicode utilities or the whole application.
