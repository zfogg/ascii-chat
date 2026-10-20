# Sync primitive inspector

In a debug build, press `0` during a session to open the live inspector.
Press `0` or Escape to return to the underlying screen.

- Up/Down select the previous/next object, crossing page boundaries.
- Left/Right move one page, preserving the row where possible and clamping at the endpoints.
  They preserve the selection when there is no previous/next page.
- Home selects the first object on the first page, even from another row on that page.
- End selects the last object on the last page. Repeated Home/End presses stay at the endpoint.
- Resizing recalculates pages around the selected object.

The header reports the total objects, current page, selected item, sample age,
and counts by type: `M` (mutex), `RW` (rwlock), `C` (condition variable), and
`A` (integral/boolean/pointer atomic). The footer shows the selected object's
registration location, address, owner/waiter, value, and last-operation age.
Blue separators use UTF-8 line characters when UTF-8 is enabled and supported,
or ASCII hyphens otherwise. Colors follow the configured log palette and
`--color-mode none` disables them.

The list contains every live synchronization object in the named registry,
including idle objects. It grows dynamically and is paginated; terminal height
does not truncate the inventory. Mirror normally has fewer objects than a
connected client because it does not create the connection's workers, transport,
queues, and authentication state. File playback, audio, and active UI screens
also affect the count. Destroyed objects are removed.
When an initializer and its owner both register a primitive, the inspector
counts that address once and displays its latest registration name.
Child names include a registration ID so multiple audio buffers remain distinguishable.

This is a registry of instrumented ascii-chat primitives, not a scan of process
memory. Raw C11 atomics, synchronization inside third-party libraries, and
unregistered implementation bookkeeping do not automatically become entries.
Embedded condition/rwlock counters are displayed in their parent object's state.
New `atomic_t`/`atomic_ptr_t` session fields need explicit registration and
unregistration before their storage is released. Register process-lifetime
atomics after the named registry has initialized. Mutex/condition/rwlock
initializers register their wrappers automatically. Audio and generic ring
buffers register their atomic fields during creation and remove them on destruction.

Run the native regressions and live-mode checks with:

```sh
cmake --build build --target ascii-chat test-sync-regressions
python tests/integration/sync_regressions.py build/bin/test-sync-regressions.exe
python tests/integration/sync_tui_modes.py --binary build/bin/ascii-chat.exe --artifacts build/sync-tui-evidence
```

The live-mode test uses generated video/audio and loopback services. Its optional
PNG exports render the captured PTY cells, alongside the exact ANSI stream and
plain text; they do not require access to the user's terminal or webcam.
