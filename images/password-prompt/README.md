# Password prompt visual evidence

The concept image was generated during the design discussion for issue #575.
The implementation images are captures of the terminal cell grid emitted by the
native Windows executable through ConPTY, rendered with the captured foreground,
background, and inverse-video attributes. The test also saves the observed text
grid beside each PNG in the build output. These captures are not generated mockups.

To reproduce the implementation captures:

```powershell
cmake --preset default -B build -DBUILD_TESTS=ON
cmake --build build --target ascii-chat
ctest --test-dir build -R '^password-prompt$' --no-tests=error --output-on-failure
```

The test writes captures under `build/password-prompt/`. It uses local TCP and
ACDS services, synthetic video, and loopback STUN/TURN endpoints. Identity checking
is bypassed only in the test subprocess environment to isolate password entry;
production trust checks are unchanged. Discovery success asserts that an accepted
session join reaches `CONNECTING_HOST`; this is not a WebRTC media delivery test.

- `client-initial.png`: initial password dialog, with Confirm selected by default.
- `client-selected-cancel.png`: Cancel selected using Right, with a filled background.
- `client-scrolled.png`: horizontally scrolled 80-character masked input.
- `client-masked.png`: two-line masked password and selected Confirm button.
- `client-newline-limit.png`: four-line input and the error after a fourth newline.
- `client-ascii.png`: ASCII borders with `--utf8 false`.
- `client-incorrect.png`: incorrect-password feedback with cleared input.
- `discovery-incorrect.png`: the same dialog after a rejected discovery session join.
- `client-narrow.png`: compact logo and popup at 40 columns by 19 rows.

The implementation uses the existing rainbow animation, popup frame, line editor,
and buttons. The title is centered in the top border; the connection text and
controls are centered within the frame. The label and confirmation wording match
the concept. Shortcut names are orange. Tab selects buttons; Left/Right and Home/End
navigate the shared scrollable input editor. Ctrl+V inserts clipboard text at
the cursor, preserving following text and the three-newline limit. Native terminal
colors, font metrics, and the input marker differ from the illustrative concept.

Tab cycles between buttons, Enter activates the selected button, and Esc cancels. Typing always edits
the password. Shift+Enter inserts a newline and grows the input; up to three
newlines (four lines) are allowed. Further newlines leave the password unchanged
and show an inline error. Button selection uses the shared UI buttons, with
the button’s green/red background fill and the regular foreground color. Enabled,
unselected Confirm is green and Cancel is red. Disabled Confirm remains gray.

Popup borders use `terminal_should_use_utf8()`: `--utf8 true` forces Unicode,
`--utf8 false` forces ASCII, and auto follows detection. Captures use the measured
font cell dimensions so adjacent Unicode box-drawing glyphs meet.
