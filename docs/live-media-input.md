# Open media during a session

In native mirror, client, and discovery sessions:

- Press **o** (or **Ctrl+O**) to open an editable path/URL prompt.
- Paste a URL or drop a file into the terminal to prefill that prompt.
- Press **v** to prefill it from the clipboard, including a file copied in Explorer or Finder.
- Press **Enter** to load the source or **Escape** to cancel.

The current source stays active while the replacement loads. A failed load retains
the previous source. Escape cancels a pending load; an external URL extractor may
need to finish before another load can begin. Once loaded, Space pauses playback,
the arrow keys seek, and **[** switches between media and the webcam.

The prompt accepts one source at a time: local paths, quoted paths, shell-escaped
spaces, local `file://` URIs, and URLs supported by the existing FFmpeg/yt-dlp
resolver. Pasted content is never executed as a shell command.

Bracketed paste is the reliable automatic path. Some terminals insert dropped
paths as ordinary keystrokes; complete path-shaped bursts are recognized too.
When a terminal splits those keystrokes across reads, press **o** before dropping
the file so every character goes into the prompt instead of session shortcuts.

Windows reads Unicode text and copied-file lists. macOS uses the system
pasteboard. Linux uses `wl-paste` from wl-clipboard or `xclip`, including
`text/uri-list` file selections. Clipboard access happens only when **v** is
pressed. Copying multiple files produces an error instead of choosing one silently.

These controls apply to interactive sessions. Finite snapshots and redirected
media input retain their command-line behavior.
