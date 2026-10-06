import type { IncomingMessage, ServerResponse } from "node:http";
import net from "node:net";

const CONNECT_TIMEOUT_MS = 3_000;

type ServerTarget = {
  host: string;
  port: number;
  protocol?: "ws" | "wss";
  path?: string;
};

type ServerResult = ServerTarget & {
  up: boolean;
  latencyMs: number;
};

export type ServerKind = "webrtc" | "stun" | "turn";

export const DEFAULT_SERVERS: Record<ServerKind, ServerTarget[]> = {
  webrtc: [{ host: "discovery-service.ascii-chat.com", port: 443 }],
  stun: [
    { host: "stun.ascii-chat.com", port: 3478 },
    { host: "stun.l.google.com", port: 19302 },
  ],
  turn: [{ host: "turn.ascii-chat.com", port: 3478 }],
};

function parseServers(
  value: string | undefined,
  fallback: ServerTarget[],
): ServerTarget[] {
  if (!value) return fallback;

  const servers = value.split(",").map((entry) => {
    const separator = entry.lastIndexOf(":");
    const host = entry.slice(0, separator).trim();
    const port = Number(entry.slice(separator + 1));
    if (!host || !Number.isInteger(port) || port < 1 || port > 65535) {
      throw new Error(`Invalid server target: ${entry}`);
    }
    return { host, port };
  });

  return servers.length > 0 ? servers : fallback;
}

export function configuredServers(kind: ServerKind): ServerTarget[] {
  if (kind === "webrtc") return configuredWebRtcServers();
  const variable = `DISCOVERY_STATUS_${kind.toUpperCase()}_SERVERS`;
  return parseServers(process.env[variable], DEFAULT_SERVERS[kind]);
}

function configuredWebRtcServers(): ServerTarget[] {
  const value = process.env.DISCOVERY_STATUS_WEBRTC_SERVERS;
  if (!value) return DEFAULT_SERVERS.webrtc;

  return value.split(",").map((entry) => {
    const target = entry.trim();
    if (!target.startsWith("ws://") && !target.startsWith("wss://")) {
      return parseServers(target, [])[0]!;
    }

    const url = new URL(target);
    const port = Number(url.port || (url.protocol === "ws:" ? 80 : 443));
    if (!url.hostname || !Number.isInteger(port) || port < 1 || port > 65535) {
      throw new Error(`Invalid WebRTC status target: ${entry}`);
    }
    return {
      host: url.hostname,
      port,
      protocol: url.protocol.slice(0, -1) as "ws" | "wss",
      path: `${url.pathname}${url.search}`,
    };
  });
}

export function checkTcpServer({
  host,
  port,
}: ServerTarget): Promise<ServerResult> {
  return new Promise((resolve) => {
    const startedAt = Date.now();
    const socket = net.createConnection({ host, port });
    let settled = false;

    const finish = (up: boolean) => {
      if (settled) return;
      settled = true;
      socket.destroy();
      resolve({ host, port, up, latencyMs: Date.now() - startedAt });
    };

    socket.setTimeout(CONNECT_TIMEOUT_MS);
    socket.once("connect", () => finish(true));
    socket.once("timeout", () => finish(false));
    socket.once("error", () => finish(false));
  });
}

function checkWebSocket({
  host,
  port,
  protocol: configuredProtocol,
  path = "",
}: ServerTarget): Promise<ServerResult> {
  return new Promise((resolve) => {
    const startedAt = Date.now();
    const protocol = configuredProtocol || (port === 80 ? "ws" : "wss");
    const socket = new WebSocket(`${protocol}://${host}:${port}${path}`);
    let settled = false;

    const finish = (up: boolean) => {
      if (settled) return;
      settled = true;
      clearTimeout(timeout);
      socket.close();
      resolve({ host, port, up, latencyMs: Date.now() - startedAt });
    };

    const timeout = setTimeout(() => finish(false), CONNECT_TIMEOUT_MS);
    socket.addEventListener("open", () => finish(true), { once: true });
    socket.addEventListener("error", () => finish(false), { once: true });
  });
}

export async function checkServers(kind: ServerKind) {
  const servers = configuredServers(kind);
  const checker = kind === "webrtc" ? checkWebSocket : checkTcpServer;
  const results = await Promise.all(servers.map(checker));
  return {
    kind,
    up: results.every((server) => server.up),
    checkedAt: new Date().toISOString(),
    servers: results,
  };
}

export async function statusHandler(
  kind: ServerKind,
  request: IncomingMessage,
  response: ServerResponse,
) {
  if (request.method !== "GET") {
    response.setHeader("Allow", "GET");
    response.writeHead(405, { "Content-Type": "application/json" });
    response.end(JSON.stringify({ error: "Method not allowed" }));
    return;
  }

  try {
    const status = await checkServers(kind);
    response.writeHead(200, {
      "Cache-Control": "no-store",
      "Content-Type": "application/json",
    });
    response.end(JSON.stringify(status));
  } catch {
    response.writeHead(500, { "Content-Type": "application/json" });
    response.end(
      JSON.stringify({ error: "Server status check is misconfigured" }),
    );
  }
}
