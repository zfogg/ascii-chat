import { spawn, type ChildProcess } from "node:child_process";
import * as dgram from "node:dgram";
import * as fs from "node:fs";
import * as net from "node:net";
import * as os from "node:os";
import * as path from "node:path";
import { expect, test, type Page } from "@playwright/test";
import {
  expectMeaningful60Fps,
  getRandomPort,
  ServerFixture,
  waitForPort,
} from "./server-fixture";

const PASSWORD = "crypto-e2e-password";

function binaryPath(): string {
  return path.resolve(
    process.env["ASCII_CHAT_TEST_BINARY"] ||
      path.join(process.cwd(), "../../build/bin/ascii-chat.exe"),
  );
}

async function unusedTcpPorts(count: number): Promise<number[]> {
  const servers = Array.from({ length: count }, () => net.createServer());
  await Promise.all(
    servers.map(
      (server) =>
        new Promise<void>((resolve, reject) => {
          server.once("error", reject);
          server.listen(0, "127.0.0.1", resolve);
        }),
    ),
  );
  const ports = servers.map((server) => {
    const address = server.address();
    if (!address || typeof address === "string")
      throw new Error("Could not reserve a TCP port");
    return address.port;
  });
  await Promise.all(
    servers.map(
      (server) => new Promise<void>((resolve) => server.close(() => resolve())),
    ),
  );
  return ports;
}

async function unusedUdpPort(): Promise<number> {
  const socket = dgram.createSocket("udp4");
  await new Promise<void>((resolve, reject) => {
    socket.once("error", reject);
    socket.bind(0, "127.0.0.1", resolve);
  });
  const address = socket.address();
  await new Promise<void>((resolve) => socket.close(() => resolve()));
  return address.port;
}

function run(command: string, args: string[]): Promise<string> {
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, { windowsHide: true });
    let output = "";
    child.stdout?.on("data", (chunk: Buffer) => (output += chunk.toString()));
    child.stderr?.on("data", (chunk: Buffer) => (output += chunk.toString()));
    child.once("error", reject);
    child.once("exit", (code) =>
      code === 0
        ? resolve(output.trim())
        : reject(new Error(`${command} exited ${code}: ${output}`)),
    );
  });
}

async function dockerNetworkIp(network: string): Promise<string> {
  const output = await run("docker", ["network", "inspect", network]);
  const parsed = JSON.parse(output) as Array<{
    IPAM: { Config: Array<{ Subnet: string }> };
  }>;
  const subnet = parsed[0]?.IPAM.Config[0]?.Subnet;
  if (!subnet) throw new Error(`Docker network ${network} has no subnet`);
  return `${subnet.split(".").slice(0, 3).join(".")}.10`;
}

async function stopProcess(child: ChildProcess | undefined): Promise<void> {
  if (!child || child.exitCode !== null) return;
  await new Promise<void>((resolve) => {
    const timer = setTimeout(() => {
      child.kill("SIGKILL");
      resolve();
    }, 5_000);
    child.once("exit", () => {
      clearTimeout(timer);
      resolve();
    });
    child.kill("SIGTERM");
  });
}

class LoggedProcess {
  readonly child: ChildProcess;
  private output = "";

  constructor(
    args: string[],
    readonly logPath: string,
  ) {
    this.child = spawn(
      binaryPath(),
      ["--log-level", "debug", "--log-file", logPath, ...args],
      { windowsHide: true, stdio: ["ignore", "pipe", "pipe"] },
    );
    for (const stream of [this.child.stdout, this.child.stderr]) {
      stream?.on("data", (chunk: Buffer) => {
        this.output += chunk.toString();
      });
    }
  }

  get logs(): string {
    try {
      return `${this.output}\n${fs.readFileSync(this.logPath, "utf8")}`;
    } catch {
      return this.output;
    }
  }

  async waitFor(pattern: RegExp, timeoutMs: number): Promise<RegExpMatchArray> {
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
      const match = this.logs.match(pattern);
      if (match) return match;
      if (this.child.exitCode !== null)
        throw new Error(
          `ascii-chat exited ${this.child.exitCode}:\n${this.logs}`,
        );
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
    throw new Error(`Timed out waiting for ${pattern}:\n${this.logs}`);
  }
}

type AuthCase = "authorized-key" | "unauthorized-key" | "password";
type Mode = "client" | "discovery";

function seedCryptoSettings(
  page: Page,
  options: {
    identity: string;
    serverPublicKey: string;
    target: "client-server" | "discovery-service";
    password?: string;
  },
): Promise<void> {
  return page.addInitScript((settings) => {
    localStorage.setItem(
      "ascii-chat.crypto-settings.v1",
      JSON.stringify({
        customEncryption: true,
        authenticationEnabled: true,
        skipServerVerification: false,
        password: settings.password || "",
        privateKeys: settings.identity
          ? [
              {
                id: "e2e-identity",
                name: "E2E identity",
                contents: settings.identity,
              },
            ]
          : [],
        activePrivateKeyId: settings.identity ? "e2e-identity" : null,
        verificationKeys: [
          {
            id: "e2e-server",
            name: "E2E server",
            contents: settings.serverPublicKey,
            target: settings.target,
          },
        ],
        activeVerificationKeyIds: { [settings.target]: "e2e-server" },
      }),
    );
  }, options);
}

async function verifyFiveSecondsOfRendering(page: Page): Promise<void> {
  await expect(page.locator(".status")).toContainText("Connected", {
    timeout: 45_000,
  });
  const initial = await page.evaluate(
    () => window.__clientFrameMetrics?.changedReceived ?? 0,
  );
  const samples: number[] = [];
  let previous = initial;
  for (let second = 0; second < 5; second++) {
    await page.waitForTimeout(1_000);
    await expect(page.locator(".status")).toContainText("Connected");
    const current = await page.evaluate(
      () => window.__clientFrameMetrics?.changedReceived ?? 0,
    );
    samples.push(current - previous);
    previous = current;
  }
  expect(
    samples.every((count) => count > 0),
    `Expected changing received frames during each of five seconds; deltas=${samples.join(",")}`,
  ).toBe(true);
  expect(previous).toBeGreaterThan(initial);
  await expectMeaningful60Fps(page, "client", 5_000);
}

async function runCryptoRenderCase(
  page: Page,
  mode: Mode,
  authCase: AuthCase,
): Promise<void> {
  test.setTimeout(120_000);
  const root = fs.mkdtempSync(
    path.join(os.tmpdir(), "ascii-crypto-render-e2e-"),
  );
  const fixtureKeys = path.join(process.cwd(), "tests/fixtures/crypto");
  const clientKey = path.join(fixtureKeys, "client-identity");
  const rejectedKey = path.join(fixtureKeys, "rejected-identity");
  const serverKey = path.join(fixtureKeys, "server-identity");
  const browserIdentity =
    authCase === "unauthorized-key" && mode === "client"
      ? fs.readFileSync(rejectedKey, "utf8")
      : authCase !== "password"
        ? fs.readFileSync(clientKey, "utf8")
        : "";
  const browserServerPublicKey =
    authCase === "unauthorized-key" && mode === "discovery"
      ? fs.readFileSync(`${rejectedKey}.pub`, "utf8")
      : fs.readFileSync(`${serverKey}.pub`, "utf8");
  const rejected = authCase === "unauthorized-key";
  const password = authCase === "password" ? PASSWORD : undefined;
  const serverAuthArgs =
    authCase === "password"
      ? ["--password", PASSWORD]
      : ["--client-keys", `${clientKey}.pub`];
  let fixture: ServerFixture | undefined;
  let acds: LoggedProcess | undefined;
  let server: ServerFixture | undefined;
  let turnStarted = false;
  let networkCreated = false;
  const network = `ascii-crypto-${process.pid}-${Date.now()}`;
  const container = network;

  try {
    if (mode === "client") {
      fixture = new ServerFixture(getRandomPort(), [
        "--key",
        serverKey,
        ...serverAuthArgs,
      ]);
      await fixture.start();
      await seedCryptoSettings(page, {
        identity: browserIdentity,
        serverPublicKey: browserServerPublicKey,
        target: "client-server",
        ...(password ? { password } : {}),
      });
      const url = `/client?connect&test2&testServerUrl=${encodeURIComponent(fixture.getUrl())}`;
      await page.goto(url);
    } else {
      const ports = await unusedTcpPorts(4);
      const acdsTcpPort = ports[0]!;
      const acdsWsPort = ports[1]!;
      const serverTcpPort = ports[2]!;
      const serverWsPort = ports[3]!;
      const turnPort = await unusedUdpPort();
      const relayStart = 52_000 + Math.floor(Math.random() * 11_000);
      const relayEnd = relayStart + 15;
      const subnetOctet = 20 + Math.floor(Math.random() * 200);
      const subnet = `10.252.${subnetOctet}.0/24`;
      await run("docker", ["network", "create", "--subnet", subnet, network]);
      networkCreated = true;
      const turnIp = await dockerNetworkIp(network);
      await run("docker", [
        "run",
        "--detach",
        "--name",
        container,
        "--network",
        network,
        "--ip",
        turnIp,
        "--publish",
        `127.0.0.1:${turnPort}:3478/udp`,
        "--publish",
        `127.0.0.1:${turnPort}:3478/tcp`,
        "--publish",
        `127.0.0.1:${relayStart}-${relayEnd}:${relayStart}-${relayEnd}/udp`,
        "coturn/coturn:latest",
        "--listening-ip=0.0.0.0",
        "--listening-port=3478",
        `--relay-ip=${turnIp}`,
        `--external-ip=127.0.0.1/${turnIp}`,
        `--min-port=${relayStart}`,
        `--max-port=${relayEnd}`,
        "--user=crypto-e2e:crypto-e2e-password",
        "--realm=ascii-chat.test",
        "--lt-cred-mech",
        "--allowed-peer-ip=169.254.0.0-169.254.255.255",
        "--fingerprint",
        "--allow-loopback-peers",
        "--no-cli",
        "--no-tls",
        "--log-file=stdout",
        "--verbose",
      ]);
      turnStarted = true;
      acds = new LoggedProcess(
        [
          "discovery-service",
          "--port",
          String(acdsTcpPort),
          "--websocket-port",
          String(acdsWsPort),
          "--database",
          path.join(root, "acds.sqlite"),
          "--key",
          serverKey,
          "--stun-servers",
          `stun:127.0.0.1:${turnPort}`,
          "--turn-servers",
          `turn:127.0.0.1:${turnPort}`,
        ],
        path.join(root, "acds.log"),
      );
      await waitForPort(acdsTcpPort, "127.0.0.1");
      await waitForPort(acdsWsPort, "127.0.0.1");
      server = new ServerFixture(
        serverTcpPort,
        [
          "server",
          "--port",
          String(serverTcpPort),
          "--websocket-port",
          String(serverWsPort),
          "--discovery",
          "--discovery-service",
          "127.0.0.1",
          "--discovery-service-port",
          String(acdsTcpPort),
          "--webrtc",
          "--stun-servers",
          "",
          "--turn-servers",
          `turn:127.0.0.1:${turnPort}`,
          "--turn-username",
          "crypto-e2e",
          "--turn-credential",
          "crypto-e2e-password",
          "--key",
          serverKey,
          ...serverAuthArgs,
        ],
        { ...process.env, APPDATA: root },
      );
      await server.start();
      const sessionMatch = await server.waitForLog(
        /Session created: ([a-z]+-[a-z]+-[a-z]+)/i,
        30_000,
      );
      await seedCryptoSettings(page, {
        identity: browserIdentity,
        serverPublicKey: browserServerPublicKey,
        target: "discovery-service",
        ...(password ? { password } : {}),
      });
      const query = new URLSearchParams({
        session: sessionMatch[1]!,
        signalingUrl: `ws://127.0.0.1:${acdsWsPort}`,
        stunUrls: `stun:127.0.0.1:${turnPort}`,
        turnUrls: `turn:127.0.0.1:${turnPort}`,
        test2: "",
      });
      await page.goto(`/discovery?${query.toString()}`);
      await page.getByText("Connection settings", { exact: true }).click();
      await page.getByLabel("TURN username").fill("crypto-e2e");
      await page.getByLabel("TURN password").fill("crypto-e2e-password");
      if (password) {
        await page
          .getByLabel("Session password", { exact: true })
          .fill(password);
      }
      await page
        .getByRole("button", { name: "Join session", exact: true })
        .click({
          force: true,
        });
    }

    if (rejected) {
      const frameCountAtRejection = await page.evaluate(
        () => window.__clientFrameMetrics?.changedReceived ?? 0,
      );
      await expect
        .poll(() => page.locator(".status").innerText(), { timeout: 45_000 })
        .toMatch(/error|failed|rejected|disconnected/i);
      for (let second = 0; second < 5; second++) {
        await page.waitForTimeout(1_000);
        await expect(page.locator(".status")).not.toContainText("Connected");
        const current = await page.evaluate(
          () => window.__clientFrameMetrics?.changedReceived ?? 0,
        );
        expect(current).toBe(frameCountAtRejection);
      }
    } else {
      await verifyFiveSecondsOfRendering(page);
    }
  } finally {
    await fixture?.stop();
    await server?.stop();
    await stopProcess(acds?.child);
    if (turnStarted) {
      await run("docker", ["stop", container]).catch(() => "");
      await run("docker", ["rm", "--force", container]).catch(() => "");
    }
    if (networkCreated)
      await run("docker", ["network", "rm", network]).catch(() => "");
    fs.rmSync(root, { recursive: true, force: true });
  }
}

for (const mode of ["client", "discovery"] as const) {
  for (const authCase of [
    "authorized-key",
    "unauthorized-key",
    "password",
  ] as const) {
    const outcome =
      authCase === "unauthorized-key"
        ? "rejects without rendering for 5s"
        : "renders changing frames for 5s";
    test(`${mode} custom crypto ${authCase} ${outcome}`, async ({ page }) => {
      await runCryptoRenderCase(page, mode, authCase);
    });
  }
}
