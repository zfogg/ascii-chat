# Same-host WebRTC tests

The discovery WebRTC integration tests normally use the host's network interfaces. On machines where UDP packets sent to the machine's own LAN address are filtered, ICE negotiation can time out even though loopback UDP works.

For local testing in that environment, use the dedicated test build:

```sh
cmake --preset webrtc-local-tests
cmake --build build-webrtc-local-tests --target ascii-chat test_integration_discovery_webrtc_discovery_e2e test_integration_discovery_webrtc_frame_capture
ctest --test-dir build-webrtc-local-tests -R '^test_integration_discovery_webrtc_(discovery_e2e|frame_capture)$' --no-tests=error --output-on-failure --parallel 1
```

This preset builds libdatachannel and libjuice from source with libjuice's localhost-candidate test option enabled. It is intended only for same-host tests; do not use its binaries for real sessions, since they advertise loopback ICE candidates. Normal builds do not enable this option.
