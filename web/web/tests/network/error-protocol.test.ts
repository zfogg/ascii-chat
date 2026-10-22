import { describe, expect, it } from "vite-plus/test";
import {
  decodeRemoteError,
  encodeBrowserError,
} from "../../src/network/errorProtocol";

describe("browser error protocol bridge", () => {
  it("encodes JavaScript errors for the server as ERROR_INTERNAL", () => {
    const payload = encodeBrowserError({
      id: 1,
      source: "JavaScript",
      title: "TypeError",
      frames: [
        { message: "encoder exploded" },
        { message: "at captureFrame (src/capture.ts:71:4)" },
      ],
    });

    expect(decodeRemoteError(payload)).toEqual({
      code: 53,
      message: "encoder exploded\nat captureFrame (src/capture.ts:71:4)",
    });
  });

  it("decodes a server stack without losing its line boundaries", () => {
    const text =
      "server wrapper (src/server/client.c:144 in run())\n" +
      "socket cause (lib/network/tcp.c:91 in receive())";
    const bytes = new TextEncoder().encode(text);
    const payload = new Uint8Array(8 + bytes.length);
    const view = new DataView(payload.buffer);
    view.setUint32(0, 49, false);
    view.setUint32(4, bytes.length, false);
    payload.set(bytes, 8);

    expect(decodeRemoteError(payload)).toEqual({ code: 49, message: text });
  });
});
