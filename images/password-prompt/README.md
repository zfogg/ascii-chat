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

- `client-initial.png`: initial password dialog, with Confirm disabled while empty.
- `client-masked.png`: masked editing and selected Confirm button.
- `client-incorrect.png`: incorrect-password feedback with cleared input.
- `discovery-incorrect.png`: the same dialog after a rejected discovery session join.
- `client-narrow.png`: compact logo and popup at 40 columns by 19 rows.

The implementation uses the existing rainbow animation, popup frame, line editor,
and buttons. Native terminal colors, font metrics, the input marker, and the
minimum-length label differ from the illustrative concept.
