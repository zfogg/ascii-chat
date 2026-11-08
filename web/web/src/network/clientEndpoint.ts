export type ClientEndpoint = {
  mode: "auto" | "quic" | "websocket";
  url: string;
};

/** The entered scheme expresses policy; WebTransport itself requires HTTPS. */
export function clientEndpoint(input: string): ClientEndpoint {
  const value = input.trim();
  const url = new URL(value);
  if (!url.hostname) throw new Error("Server URL needs a host");
  if (url.username || url.password || url.hash)
    throw new Error("Server URLs cannot contain credentials or fragments");
  if (url.protocol === "ws:" || url.protocol === "wss:")
    return { mode: "websocket", url: url.href };
  if (url.protocol === "https:") return { mode: "auto", url: url.href };
  if (url.protocol !== "quic:")
    throw new Error("Use a quic://, ws://, wss://, or https:// server URL");
  // Changing URL.protocol cannot convert a custom scheme to a special scheme.
  const endpoint = new URL(`https://${url.host}${url.pathname}${url.search}`);
  if (!url.port) endpoint.port = "27224";
  if (!url.pathname || url.pathname === "/") endpoint.pathname = "/acip";
  return { mode: "quic", url: endpoint.href };
}
