export type ServerKind = "webrtc" | "stun" | "turn";

export type ServerTarget = {
  host: string;
  port: number;
  protocol?: "ws" | "wss";
  transport?: "udp" | "tls";
  path?: string;
};

export const SERVER_ENVIRONMENT_VARIABLES: Record<ServerKind, string> = {
  webrtc: "DISCOVERY_WEBRTC_SERVERS",
  stun: "DISCOVERY_STUN_SERVERS",
  turn: "DISCOVERY_TURN_SERVERS",
};

const DEFAULT_SERVER_LISTS: Record<ServerKind, string> = {
  webrtc: "wss://discovery-service.ascii-chat.com:443",
  stun: "stun.ascii-chat.com:3478,stun.l.google.com:19302",
  turn: "turn.ascii-chat.com:3478",
};

function parsePort(value: string, entry: string) {
  const port = Number(value);
  if (!Number.isInteger(port) || port < 1 || port > 65535) {
    throw new Error(`Invalid server target: ${entry}`);
  }
  return port;
}

function parseWebRtcTarget(entry: string): ServerTarget {
  const value = entry.trim();
  const url = new URL(
    value.startsWith("ws://") || value.startsWith("wss://")
      ? value
      : `wss://${value}`,
  );
  if (url.protocol !== "ws:" && url.protocol !== "wss:" || !url.hostname) {
    throw new Error(`Invalid WebRTC server target: ${entry}`);
  }
  return {
    host: url.hostname,
    port: parsePort(url.port || (url.protocol === "ws:" ? "80" : "443"), entry),
    protocol: url.protocol.slice(0, -1) as "ws" | "wss",
    path: `${url.pathname === "/" ? "" : url.pathname}${url.search}`,
  };
}

function parseIceTarget(kind: "stun" | "turn", entry: string): ServerTarget {
  const rawValue = entry.trim();
  const secure = rawValue.toLowerCase().startsWith(`${kind}s:`);
  const value = rawValue.replace(new RegExp(`^${kind}s?:`, "i"), "");
  const url = new URL(`${kind}://${value}`);
  if (!url.hostname) throw new Error(`Invalid ${kind.toUpperCase()} server target: ${entry}`);
  return {
    host: url.hostname,
    port: parsePort(url.port || (secure ? "5349" : "3478"), entry),
    transport: secure ? "tls" : "udp",
  };
}

export function configuredServerTargets(
  kind: ServerKind,
  configuredValue?: string,
): ServerTarget[] {
  const value = configuredValue?.trim() || DEFAULT_SERVER_LISTS[kind];
  const entries = value.split(",").map((entry) => entry.trim()).filter(Boolean);
  if (entries.length === 0) return configuredServerTargets(kind);
  return entries.map((entry) =>
    kind === "webrtc"
      ? parseWebRtcTarget(entry)
      : parseIceTarget(kind, entry),
  );
}

export function serverTargetUrl(kind: ServerKind, target: ServerTarget) {
  if (kind === "webrtc") {
    const protocol = target.protocol || (target.port === 80 ? "ws" : "wss");
    return `${protocol}://${target.host}:${target.port}${target.path || ""}`;
  }
  return `${kind}${target.transport === "tls" ? "s" : ""}:${target.host}:${target.port}`;
}
