import { describe, expect, it } from "vite-plus/test";
import { clientEndpoint } from "../../src/network/clientEndpoint";

describe("client endpoint selection", () => {
  it.each([
    ["quic://chat.example", "https://chat.example:27224/acip"],
    ["quic://chat.example:443", "https://chat.example/acip"],
    ["quic://[::1]:4443", "https://[::1]:4443/acip"],
    [
      "quic://chat.example:4443/custom?room=one",
      "https://chat.example:4443/custom?room=one",
    ],
  ])("maps explicit QUIC %s to the browser endpoint", (input, url) => {
    expect(clientEndpoint(input)).toEqual({ mode: "quic", url });
  });

  it.each(["ws://localhost:27226/", "wss://chat.example:4443/ws"])(
    "preserves explicit WebSocket endpoint %s",
    (url) => expect(clientEndpoint(url)).toEqual({ mode: "websocket", url }),
  );

  it("uses HTTPS as Auto without inventing a fallback address", () => {
    expect(clientEndpoint("https://chat.example:4443/acip")).toEqual({
      mode: "auto",
      url: "https://chat.example:4443/acip",
    });
  });

  it.each([
    "http://chat.example",
    "tcp://chat.example",
    "chat.example",
    "quic:///acip",
    "quic://user:password@chat.example",
    "wss://chat.example/#fragment",
  ])("rejects invalid endpoint %s", (url) =>
    expect(() => clientEndpoint(url)).toThrow(),
  );
});
