import { describe, expect, it } from "vite-plus/test";
import {
  decodeRemoteError,
  encodeBrowserError,
  presentRemoteError,
  RemoteErrorAction,
} from "../../src/network/errorProtocol";

describe("browser error protocol bridge", () => {
  it("presents a malformed rejection once as a fatal protocol error", () => {
    const reports: string[] = [];
    const listener = (event: Event) =>
      reports.push((event as CustomEvent).detail.title);
    window.addEventListener("ascii-chat:error", listener);
    try {
      expect(presentRemoteError(new Uint8Array(7))).toBe(
        RemoteErrorAction.Stop,
      );
      expect(reports).toEqual(["Remote protocol error 44"]);
    } finally {
      window.removeEventListener("ascii-chat:error", listener);
    }
  });
  it.each([7, 9, 4105])(
    "rejects malformed or oversized payloads (%i bytes)",
    (size) => {
      const payload = new Uint8Array(size);
      if (size >= 8)
        new DataView(payload.buffer).setUint32(4, size === 4105 ? size - 8 : 0);
      expect(() => decodeRemoteError(payload)).toThrow();
    },
  );
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
