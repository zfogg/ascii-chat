import type { IncomingMessage, ServerResponse } from "node:http";
import { connect as connectTls } from "node:tls";
import {
  classes,
  createTurnClient,
  Message,
  methods,
  parseMessage,
  StunProtocol,
} from "werift-ice";
import {
  configuredServerTargets,
  SERVER_ENVIRONMENT_VARIABLES,
  type ServerKind,
  type ServerTarget,
} from "../src/config/serverEndpoints.js";

const PROBE_TIMEOUT_MS = 3_000;
const STUN_RETRANSMISSIONS = 2;
const STUN_RESPONSE_TIMEOUT_MS =
  PROBE_TIMEOUT_MS / (2 ** (STUN_RETRANSMISSIONS + 1) - 1);

type ServerResult = ServerTarget & {
  up: boolean;
  latencyMs: number;
};

export function configuredServers(kind: ServerKind): ServerTarget[] {
  return configuredServerTargets(
    kind,
    process.env[SERVER_ENVIRONMENT_VARIABLES[kind]],
  );
}

async function checkStunServer({
  host,
  port,
  transport,
}: ServerTarget): Promise<ServerResult> {
  if (transport === "tls") return checkStunTlsServer({ host, port, transport });

  const startedAt = Date.now();
  const protocol = new StunProtocol();

  try {
    await protocol.connectionMade(true);
    await protocol.request(
      new Message(methods.BINDING, classes.REQUEST),
      [host, port],
      undefined,
      {
        retransmissions: STUN_RETRANSMISSIONS,
        responseTimeout: STUN_RESPONSE_TIMEOUT_MS,
      },
    );
    return { host, port, up: true, latencyMs: Date.now() - startedAt };
  } catch {
    return { host, port, up: false, latencyMs: Date.now() - startedAt };
  } finally {
    await protocol.close();
  }
}

function checkStunTlsServer({ host, port }: ServerTarget): Promise<ServerResult> {
  return new Promise((resolve) => {
    const startedAt = Date.now();
    const request = new Message(methods.BINDING, classes.REQUEST);
    const socket = connectTls({ host, port, servername: host });
    let response = Buffer.alloc(0);
    let settled = false;

    const finish = (up: boolean) => {
      if (settled) return;
      settled = true;
      socket.destroy();
      resolve({
        host,
        port,
        transport: "tls",
        up,
        latencyMs: Date.now() - startedAt,
      });
    };

    const timeout = setTimeout(() => finish(false), PROBE_TIMEOUT_MS);
    socket.once("secureConnect", () => socket.write(request.bytes));
    socket.once("error", () => finish(false));
    socket.on("data", (chunk) => {
      response = Buffer.concat([response, Buffer.from(chunk)]);
      while (response.length >= 20) {
        const length = response.readUInt16BE(2);
        const messageLength = 20 + length;
        if (response.length < messageLength) return;
        const message = parseMessage(response.subarray(0, messageLength));
        response = response.subarray(messageLength);
        if (message && message.transactionId.equals(request.transactionId)) {
          clearTimeout(timeout);
          finish(true);
          return;
        }
      }
    });
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

    const timeout = setTimeout(() => finish(false), PROBE_TIMEOUT_MS);
    socket.addEventListener("open", () => finish(true), { once: true });
    socket.addEventListener("error", () => finish(false), { once: true });
  });
}

async function checkTurnServer({
  host,
  port,
  transport,
}: ServerTarget): Promise<ServerResult> {
  const startedAt = Date.now();
  const username = process.env["DISCOVERY_STATUS_TURN_USERNAME"];
  const password = process.env["DISCOVERY_STATUS_TURN_PASSWORD"];

  if (!username || !password) {
    return { host, port, up: false, latencyMs: Date.now() - startedAt };
  }

  let client: Awaited<ReturnType<typeof createTurnClient>> | undefined;
  try {
    client = await createTurnClient(
      { address: [host, port], username, password },
      {
        lifetime: 60,
        transport: transport === "tls" ? "tls" : "udp",
        tlsOptions: transport === "tls" ? { servername: host } : undefined,
      },
    );
    return { host, port, up: true, latencyMs: Date.now() - startedAt };
  } catch {
    return { host, port, up: false, latencyMs: Date.now() - startedAt };
  } finally {
    await client?.close();
  }
}

export async function checkServers(kind: ServerKind) {
  const servers = configuredServers(kind);
  const checker =
    kind === "webrtc"
      ? checkWebSocket
      : kind === "turn"
        ? checkTurnServer
        : checkStunServer;
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
