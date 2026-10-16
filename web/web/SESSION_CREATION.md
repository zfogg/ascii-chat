# Browser-created Discovery sessions

Discovery offers Join and Create session in one form. Create registers a private
WebRTC session with ACDS and joins as its first participant, matching CLI discovery.
The creating browser hosts the call; no native media server or host-management API
is involved. ACDS provides discovery, password validation, and signaling only.

The browser compiles the CLI's `src/common/session/host.c` into WebAssembly. Its
transport adapter accepts ACIP packets and schedules the shared C rendering step.
Participants upload video using the existing H.264/HEVC encoders. WebCodecs decodes
compressed uploads on the host into pixels; the shared C host converts/composites
those pixels and sends ACIP ASCII_FRAME packets to every participant, including the
host's local display. Explicit codec selection retains actionable unsupported-codec
errors. Host election and migration are not implemented yet: closing the creator
ends the hosted media connection.

Creation uses the shared C discovery signing code and an Argon2 password hash.
Leave the password blank for an unprotected session. After creation succeeds, the
invitation card shows the confirmed name, Copy name, and Copy join link. The link
includes the password in its `#sessionPassword=...` fragment when provided. It also
preserves the discovery address and non-secret routing overrides. Use reachable
web/discovery addresses when inviting someone on a different computer.
