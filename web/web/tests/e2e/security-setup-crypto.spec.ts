import { execFileSync, spawn, type ChildProcess } from "node:child_process";
import { mkdtempSync, readFileSync, rmSync } from "node:fs";
import * as net from "node:net";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { expect, test } from "@playwright/test";
import { getRandomPort, ServerFixture, waitForPort } from "./server-fixture";

async function unusedTcpPorts(count: number): Promise<number[]> {
  const servers = Array.from({ length: count }, () => net.createServer());
  await Promise.all(servers.map((server) => new Promise<void>((resolve, reject) => {
    server.once("error", reject);
    server.listen(0, "127.0.0.1", resolve);
  })));
  const ports = servers.map((server) => {
    const address = server.address();
    if (!address || typeof address === "string") throw new Error("No TCP port");
    return address.port;
  });
  await Promise.all(servers.map((server) => new Promise<void>((resolve) => server.close(() => resolve()))));
  return ports;
}

function getAsciiChatBinaryPath(): string {
  return process.env["ASCII_CHAT_TEST_BINARY"] ||
    join(process.cwd(), "../../build/bin/ascii-chat.exe");
}

test("Crypto validates SSH keys, shares settings across network modes, and exposes Client password", async ({ page }) => {
  const keyDir = mkdtempSync(join(tmpdir(), "ascii-chat-browser-keys-"));
  const keyPath = join(keyDir, "identity");
  try {
    execFileSync("ssh-keygen", ["-q", "-t", "ed25519", "-N", "", "-f", keyPath]);
    const privateKey = readFileSync(keyPath, "utf8");
    const publicKey = readFileSync(`${keyPath}.pub`, "utf8");

    await page.goto("/discovery");
    // The page continuously repaints its ASCII preview, so bypass Playwright's
    // two-frame "stable element" wait for these stationary form controls.
    await page.getByRole("button", { name: "Crypto" }).click({ force: true });
    await page.getByRole("button", { name: "＋ Add private key" }).click({ force: true });
    await page.getByLabel("Private key text").fill("not an SSH private key");
    await page.getByRole("button", { name: "Add to list" }).click({ force: true });
    await expect(page.getByRole("alert")).toContainText(/Invalid|not a valid/i);

    await page.getByLabel("Private key text").fill(privateKey);
    await page.getByRole("button", { name: "Add to list" }).click({ force: true });
    await expect(page.getByText(/Identity 1.*Active/)).toBeVisible();

    await page.getByRole("button", { name: "＋ Add verification key" }).click({ force: true });
    await page.getByLabel("Verification key text").fill("ssh-ed25519 not-base64 broken-key");
    await page.getByRole("button", { name: "Add to list" }).click({ force: true });
    await expect(page.getByRole("alert")).toContainText(/Invalid|not a valid|malformed|decode/i);

    await page.getByLabel("Verification key text").fill(publicKey);
    await page.getByRole("button", { name: "Add to list" }).click({ force: true });
    await expect(page.getByText(/Verification key 1.*Active/)).toBeVisible();
    await expect(page.getByText(/SHA256:/).first()).toBeVisible();
    await page.getByLabel("Custom crypto password").fill("modal-shared-password");
    await page.getByRole("button", { name: "Save security settings" }).click({ force: true });

    await page.goto("/client");
    await expect(page.getByLabel("Crypto password")).toHaveValue("modal-shared-password");
    await page.getByRole("button", { name: "Crypto" }).click({ force: true });
    await expect(page.getByText(/Identity 1.*Active/)).toBeVisible();
    await expect(page.getByText(/Verification key 1.*Active/)).toBeVisible();
    await page.getByRole("button", { name: "Close" }).click({ force: true });

    const password = page.getByLabel("Crypto password");
    await expect(password).toBeVisible();
    await password.fill("browser-test-password");
    const passwordBox = await password.boundingBox();
    const connectBox = await page.getByRole("button", { name: "Connect", exact: true }).boundingBox();
    expect(passwordBox).not.toBeNull();
    expect(connectBox).not.toBeNull();
    expect(connectBox!.x - (passwordBox!.x + passwordBox!.width)).toBeGreaterThanOrEqual(0);
    expect(connectBox!.x - (passwordBox!.x + passwordBox!.width)).toBeLessThan(40);
  } finally {
    rmSync(keyDir, { recursive: true, force: true });
  }
});

test("Client completes a real custom-crypto handshake with client authentication and server verification", async ({
  page,
}) => {
  test.setTimeout(120_000);
  const keyDir = mkdtempSync(join(tmpdir(), "ascii-chat-handshake-keys-"));
  const clientKeyPath = join(keyDir, "client-identity");
  const serverKeyPath = join(keyDir, "server-identity");
  execFileSync("ssh-keygen", ["-q", "-t", "ed25519", "-N", "", "-f", clientKeyPath]);
  execFileSync("ssh-keygen", ["-q", "-t", "ed25519", "-N", "", "-f", serverKeyPath]);
  const server = new ServerFixture(getRandomPort(), [
    "--key",
    serverKeyPath,
    "--client-keys",
    `${clientKeyPath}.pub`,
  ]);

  try {
    await server.start();
    await page.goto(`/client?serverUrl=${encodeURIComponent(server.getUrl())}`);
    await page.getByRole("button", { name: "Crypto" }).click({ force: true });
    await page.getByRole("button", { name: "＋ Add private key" }).click({ force: true });
    await page.getByLabel("Private key text").fill(readFileSync(clientKeyPath, "utf8"));
    await page.getByRole("button", { name: "Add to list" }).click({ force: true });
    await expect(page.getByText(/Identity 1.*Active/)).toBeVisible();

    await page.getByRole("button", { name: "＋ Add verification key" }).click({ force: true });
    await page.getByLabel("Verification key text").fill(readFileSync(`${serverKeyPath}.pub`, "utf8"));
    await page.getByRole("button", { name: "Add to list" }).click({ force: true });
    await expect(page.getByText(/Verification key 1.*Active/)).toBeVisible();
    await page.getByRole("button", { name: "Save security settings" }).click({ force: true });

    await page.getByRole("button", { name: "Connect", exact: true }).click({ force: true });
    await expect(page.locator(".status")).toHaveText("Connected", { timeout: 45_000 });
    const logPath = `C:\\tmp\\ascii-chat-server-${server.getPort()}.log`;
    await expect.poll(() => {
      try {
        const logs = readFileSync(logPath, "utf8");
        return logs.includes("Client key authentication successful (whitelist verified)") &&
          logs.includes("mutual authentication");
      } catch {
        return false;
      }
    }, { timeout: 10_000 }).toBe(true);
  } finally {
    await server.stop();
    rmSync(keyDir, { recursive: true, force: true });
  }
});

test("Client rejects a valid identity that the server did not authorize", async ({ page }) => {
  test.setTimeout(90_000);
  const keyDir = mkdtempSync(join(tmpdir(), "ascii-chat-unauthorized-identity-"));
  const authorizedKeyPath = join(keyDir, "authorized-identity");
  const rejectedKeyPath = join(keyDir, "rejected-identity");
  const serverKeyPath = join(keyDir, "server-identity");
  execFileSync("ssh-keygen", ["-q", "-t", "ed25519", "-N", "", "-f", authorizedKeyPath]);
  execFileSync("ssh-keygen", ["-q", "-t", "ed25519", "-N", "", "-f", rejectedKeyPath]);
  execFileSync("ssh-keygen", ["-q", "-t", "ed25519", "-N", "", "-f", serverKeyPath]);
  const server = new ServerFixture(getRandomPort(), [
    "--key", serverKeyPath,
    "--client-keys", `${authorizedKeyPath}.pub`,
  ]);

  try {
    await server.start();
    await page.goto(`/client?serverUrl=${encodeURIComponent(server.getUrl())}`);
    await page.getByRole("button", { name: "Crypto" }).click({ force: true });
    await page.getByRole("button", { name: "＋ Add private key" }).click({ force: true });
    await page.getByLabel("Private key text").fill(readFileSync(rejectedKeyPath, "utf8"));
    await page.getByRole("button", { name: "Add to list" }).click({ force: true });
    await expect(page.getByText(/Identity 1.*Active/)).toBeVisible();
    await page.getByRole("button", { name: "＋ Add verification key" }).click({ force: true });
    await page.getByLabel("Verification key text").fill(readFileSync(`${serverKeyPath}.pub`, "utf8"));
    await page.getByRole("button", { name: "Add to list" }).click({ force: true });
    await expect(page.getByText(/Verification key 1.*Active/)).toBeVisible();
    await page.getByRole("button", { name: "Save security settings" }).click({ force: true });

    await page.getByRole("button", { name: "Connect", exact: true }).click({ force: true });
    const logPath = `C:\\tmp\\ascii-chat-server-${server.getPort()}.log`;
    await expect.poll(() => {
      try {
        return readFileSync(logPath, "utf8").includes("Client Ed25519 key not in whitelist");
      } catch {
        return false;
      }
    }, { timeout: 30_000 }).toBe(true);
    await expect(page.locator(".status")).not.toHaveText("Connected");
  } finally {
    await server.stop();
    rmSync(keyDir, { recursive: true, force: true });
  }
});

test("Discovery authenticates and verifies a real custom-crypto ACDS handshake", async ({ page }) => {
  test.setTimeout(60_000);
  const keyDir = mkdtempSync(join(tmpdir(), "ascii-chat-discovery-crypto-"));
  const clientKeyPath = join(keyDir, "client-identity");
  const serverKeyPath = join(keyDir, "server-identity");
  const databasePath = join(keyDir, "acds.sqlite");
  const logPath = join(keyDir, "acds.log");
  const ports = await unusedTcpPorts(2);
  const tcpPort = ports[0]!;
  const websocketPort = ports[1]!;
  execFileSync("ssh-keygen", ["-q", "-t", "ed25519", "-N", "", "-f", clientKeyPath]);
  execFileSync("ssh-keygen", ["-q", "-t", "ed25519", "-N", "", "-f", serverKeyPath]);

  let service: ChildProcess | undefined;
  let serviceLogs = "";
  const browserLogs: string[] = [];
  try {
    page.on("console", (message) => browserLogs.push(`[${message.type()}] ${message.text()}`));
    page.on("pageerror", (error) => browserLogs.push(`[pageerror] ${error.stack || error.message}`));
    service = spawn(getAsciiChatBinaryPath(), [
      "--log-level", "debug",
      "--log-file", logPath,
      "discovery-service",
      "--port", String(tcpPort),
      "--websocket-port", String(websocketPort),
      "--database", databasePath,
      "--key", serverKeyPath,
      "--require-client-identity",
    ], { windowsHide: true, stdio: ["ignore", "pipe", "pipe"] });
    for (const stream of [service.stdout, service.stderr]) {
      stream?.on("data", (chunk: Buffer) => { serviceLogs += chunk.toString(); });
    }
    await waitForPort(tcpPort, "127.0.0.1");
    await waitForPort(websocketPort, "127.0.0.1");

    const query = new URLSearchParams({
      session: "crypto-browser-test",
      signalingUrl: `ws://127.0.0.1:${websocketPort}`,
    });
    await page.goto(`/discovery?${query}`);
    await page.getByRole("button", { name: "Crypto" }).click({ force: true });
    await page.getByRole("button", { name: "＋ Add private key" }).click({ force: true });
    await page.getByLabel("Private key text").fill(readFileSync(clientKeyPath, "utf8"));
    await page.getByRole("button", { name: "Add to list" }).click({ force: true });
    await expect(page.getByText(/Identity 1.*Active/)).toBeVisible();
    await page.getByRole("button", { name: "＋ Add verification key" }).click({ force: true });
    await page.getByLabel("Verification key target").selectOption("discovery-service");
    await page.getByLabel("Verification key text").fill(readFileSync(`${serverKeyPath}.pub`, "utf8"));
    await page.getByRole("button", { name: "Add to list" }).click({ force: true });
    await expect(page.getByText(/Verification key 1.*Active/)).toBeVisible();
    await page.getByRole("button", { name: "Save security settings" }).click({ force: true });
    await page.getByRole("button", { name: "Join session" }).click({ force: true });

    let handshakeOutcome = "pending";
    try {
      await expect.poll(() => {
        try {
          serviceLogs = `${serviceLogs}\n${readFileSync(logPath, "utf8")}`;
        } catch {
          // The service log is created after its first logging call.
        }
        if (serviceLogs.includes("Crypto handshake completed for WebSocket client")) {
          handshakeOutcome = "completed";
          return "completed";
        }
        if (serviceLogs.includes("Crypto handshake failed for WebSocket client")) {
          handshakeOutcome = "failed";
          return "failed";
        }
        return "pending";
      }, { timeout: 30_000 }).toMatch(/completed|failed/);
    } catch (error) {
      const recent = serviceLogs
        .split(/\r?\n/)
        .filter((line) => /handshake|identity|verification|auth response|CRYPTO_|signature|failed|rejected/i.test(line))
        .slice(-25)
        .join("\n");
      console.error("[discovery-crypto-diagnostics]", recent || serviceLogs.slice(-8000));
      console.error("[discovery-crypto-page]", await page.locator("body").innerText());
      console.error("[discovery-crypto-browser]", browserLogs.slice(-60).join("\n"));
      throw error;
    }
    const handshakeDetails = serviceLogs
      .split(/\r?\n/)
      .filter((line) => /handshake|identity|verification|auth|failed|error/i.test(line))
      .slice(-40)
      .join("\n");
    expect(handshakeOutcome, handshakeDetails).toBe("completed");
  } finally {
    if (service && service.exitCode === null) {
      await new Promise<void>((resolve) => {
        const timer = setTimeout(() => { service?.kill("SIGKILL"); resolve(); }, 5000);
        service?.once("exit", () => { clearTimeout(timer); resolve(); });
        service?.kill("SIGTERM");
      });
    }
    rmSync(keyDir, { recursive: true, force: true });
  }
});
