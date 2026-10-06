import type { IncomingMessage, ServerResponse } from "node:http";
import net from "node:net";

const CONNECT_TIMEOUT_MS = 3_000;

type ServerTarget = {
  host: string;
  port: number;
};

type ServerResult = ServerTarget & {
  up: boolean;
  latencyMs: number;
};

export type ServerKind = "webrtc" | "stun" | "turn";

export const DEFAULT_SERVERS: Record<ServerKind, ServerTarget[]> = {
  webrtc: [{ host: "discovery-service.ascii-chat.com", port: 27225 }],
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
  const variable = `DISCOVERY_STATUS_${kind.toUpperCase()}_SERVERS`;
  return parseServers(process.env[variable], DEFAULT_SERVERS[kind]);
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

export async function checkServers(kind: ServerKind) {
  const servers = configuredServers(kind);
  const results = await Promise.all(servers.map(checkTcpServer));
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
