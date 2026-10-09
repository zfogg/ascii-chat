import { execFileSync, spawn, type ChildProcess } from "node:child_process";
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import * as net from "node:net";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { expect, test } from "@playwright/test";
import {
  expectMeaningful60Fps,
  getRandomPort,
  ServerFixture,
  waitForPort,
} from "./server-fixture";

async function expectKeyActive(
  page: import("@playwright/test").Page,
  keyName: string,
): Promise<void> {
  const row = page.locator(".crypto-key-row").filter({ hasText: keyName });
  await expect(
    row.getByRole("button", { name: "Active", exact: true }),
  ).toBeVisible();
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
    if (!address || typeof address === "string") throw new Error("No TCP port");
    return address.port;
  });
  await Promise.all(
    servers.map(
      (server) => new Promise<void>((resolve) => server.close(() => resolve())),
    ),
  );
  return ports;
}

function getAsciiChatBinaryPath(): string {
  return (
    process.env["ASCII_CHAT_TEST_BINARY"] ||
    join(process.cwd(), "../../build/bin/ascii-chat.exe")
  );
}

async function expectClientRendersChangingFrames(
  page: import("@playwright/test").Page,
): Promise<void> {
  await expect
    .poll(
      () =>
        page.evaluate(() => window.__clientFrameMetrics?.uniqueRendered ?? 0),
      { timeout: 15_000 },
    )
    .toBeGreaterThan(1);

  let previousChangedFrames = await page.evaluate(
    () => window.__clientFrameMetrics?.changedReceived ?? 0,
  );
  for (let second = 0; second < 3; second++) {
    await page.waitForTimeout(1_000);
    await expect(page.locator(".status")).toContainText("Connected");
    const changedFrames = await page.evaluate(
      () => window.__clientFrameMetrics?.changedReceived ?? 0,
    );
    expect(
      changedFrames,
      `No new server ASCII frames in second ${second + 1}`,
    ).toBeGreaterThan(previousChangedFrames);
    previousChangedFrames = changedFrames;
  }
  await expectMeaningful60Fps(page, "client", 3_000);

  await expect
    .poll(
      async () =>
        page.locator("canvas.ascii-canvas").evaluate((element) => {
          const canvas = element as HTMLCanvasElement;
          if (!canvas.width || !canvas.height) return false;
          const context = canvas.getContext("2d");
          if (!context) return false;
          const pixels = context.getImageData(
            0,
            0,
            canvas.width,
            canvas.height,
          ).data;
          for (let i = 0; i < pixels.length; i += 4) {
            if (pixels[i]! > 8 || pixels[i + 1]! > 8 || pixels[i + 2]! > 8)
              return true;
          }
          return false;
        }),
      { timeout: 5_000 },
    )
    .toBe(true);
}

test("Crypto validates SSH keys, shares settings across network modes, and exposes Client password", async ({
  page,
}) => {
  const keyDir = mkdtempSync(join(tmpdir(), "ascii-chat-browser-keys-"));
  const keyPath = join(keyDir, "identity");
  try {
    execFileSync("ssh-keygen", [
      "-q",
      "-t",
      "ed25519",
      "-N",
      "",
      "-f",
      keyPath,
    ]);
    const privateKey = readFileSync(keyPath, "utf8");
    const publicKey = readFileSync(`${keyPath}.pub`, "utf8");

    await page.goto("/discovery");
    // The page continuously repaints its ASCII preview, so bypass Playwright's
    // two-frame "stable element" wait for these stationary form controls.
    await page.getByRole("button", { name: "Crypto" }).click({ force: true });
    await page
      .getByRole("button", { name: "＋ Add private key" })
      .click({ force: true });
    await page.getByLabel("Private key text").fill("not an SSH private key");
    await page
      .getByRole("button", { name: "Add to list" })
      .click({ force: true });
    await expect(page.getByRole("alert")).toContainText(/Invalid|not a valid/i);

    await page.getByLabel("Private key text").fill(privateKey);
    await page
      .getByRole("button", { name: "Add to list" })
      .click({ force: true });
    await expectKeyActive(page, "Identity 1");

    await page
      .getByRole("button", { name: "＋ Add verification key" })
      .click({ force: true });
    await page
      .getByLabel("Verification key text")
      .fill("ssh-ed25519 not-base64 broken-key");
    await page
      .getByRole("button", { name: "Add to list" })
      .click({ force: true });
    await expect(page.getByRole("alert")).toContainText(
      /Invalid|not a valid|malformed|decode/i,
    );

    await page.getByLabel("Verification key text").fill(publicKey);
    await page
      .getByRole("button", { name: "Add to list" })
      .click({ force: true });
    await expectKeyActive(page, "Verification key 1");
    await expect(page.getByText(/SHA256:/).first()).toBeVisible();
    await page
      .getByLabel("Custom crypto password")
      .fill("modal-shared-password");
    await page
      .getByRole("button", { name: "Save security settings" })
      .click({ force: true });

    await page.goto("/client");
    await expect(page.getByLabel("Crypto password")).toHaveValue(
      "modal-shared-password",
    );
    await page.getByRole("button", { name: "Crypto" }).click({ force: true });
    await expectKeyActive(page, "Identity 1");
    await expectKeyActive(page, "Verification key 1");
    await page.getByRole("button", { name: "Close" }).click({ force: true });

    const password = page.getByLabel("Crypto password");
    await expect(password).toBeVisible();
    await password.fill("browser-test-password");
    const passwordBox = await password.boundingBox();
    const connectBox = await page
      .getByRole("button", { name: "Connect", exact: true })
      .boundingBox();
    expect(passwordBox).not.toBeNull();
    expect(connectBox).not.toBeNull();
    expect(
      connectBox!.x - (passwordBox!.x + passwordBox!.width),
    ).toBeGreaterThanOrEqual(0);
    expect(connectBox!.x - (passwordBox!.x + passwordBox!.width)).toBeLessThan(
      40,
    );
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
  execFileSync("ssh-keygen", [
    "-q",
    "-t",
    "ed25519",
    "-N",
    "",
    "-f",
    clientKeyPath,
  ]);
  execFileSync("ssh-keygen", [
    "-q",
    "-t",
    "ed25519",
    "-N",
    "",
    "-f",
    serverKeyPath,
  ]);
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
    await page
      .getByRole("button", { name: "＋ Add private key" })
      .click({ force: true });
    await page
      .getByLabel("Private key text")
      .fill(readFileSync(clientKeyPath, "utf8"));
    await page
      .getByRole("button", { name: "Add to list" })
      .click({ force: true });
    await expectKeyActive(page, "Identity 1");

    await page
      .getByRole("button", { name: "＋ Add verification key" })
      .click({ force: true });
    await page
      .getByLabel("Verification key text")
      .fill(readFileSync(`${serverKeyPath}.pub`, "utf8"));
    await page
      .getByRole("button", { name: "Add to list" })
      .click({ force: true });
    await expectKeyActive(page, "Verification key 1");
    await page
      .getByRole("button", { name: "Save security settings" })
      .click({ force: true });

    await page.goto(
      `/client?test2&test&testServerUrl=${encodeURIComponent(server.getUrl())}`,
    );
    await expect(page.locator(".status")).toHaveText("Connected", {
      timeout: 45_000,
    });
    await expectClientRendersChangingFrames(page);
    const logPath = `C:\\tmp\\ascii-chat-server-${server.getPort()}.log`;
    await expect
      .poll(
        () => {
          try {
            const logs = readFileSync(logPath, "utf8");
            return (
              logs.includes(
                "Client key authentication successful (whitelist verified)",
              ) && /CRYPTO_CHECK:.*crypto_ready=1.*no_encrypt=0/.test(logs)
            );
          } catch {
            return false;
          }
        },
        { timeout: 10_000 },
      )
      .toBe(true);
  } finally {
    await server.stop();
    rmSync(keyDir, { recursive: true, force: true });
  }
});

for (const keyFormat of ["armored", "binary"] as const) {
  test(`Client uses ${keyFormat} GPG Ed25519 keys for an authorized custom-crypto handshake`, async ({
    page,
  }) => {
    test.setTimeout(120_000);
    const keyDir = mkdtempSync(join(tmpdir(), "ascii-chat-gpg-handshake-"));
    const clientUid = "ASCII Chat Browser Client <client@example.test>";
    const serverUid = "ASCII Chat Test Server <server@example.test>";
    const keyExtension = keyFormat === "armored" ? "asc" : "gpg";
    const clientKeyPath = join(keyDir, `client-secret.${keyExtension}`);
    const clientPublicKeyPath = join(keyDir, `client-public.${keyExtension}`);
    const serverKeyPath = join(keyDir, `server-secret.${keyExtension}`);
    const gpgArgs = [
      "--batch",
      "--homedir",
      keyDir,
      "--pinentry-mode",
      "loopback",
      "--passphrase",
      "",
    ];

    try {
      for (const uid of [clientUid, serverUid]) {
        execFileSync("gpg", [
          ...gpgArgs,
          "--quick-generate-key",
          uid,
          "ed25519",
          "sign",
          "0",
        ]);
      }
      const armorArgs = keyFormat === "armored" ? ["--armor"] : [];
      const clientPublicKey = execFileSync("gpg", [
        ...gpgArgs,
        ...armorArgs,
        "--export",
        clientUid,
      ]);
      const clientPrivateKey = execFileSync("gpg", [
        ...gpgArgs,
        ...armorArgs,
        "--export-secret-keys",
        clientUid,
      ]);
      const serverPublicKey = execFileSync("gpg", [
        ...gpgArgs,
        ...armorArgs,
        "--export",
        serverUid,
      ]);
      const serverPrivateKey = execFileSync("gpg", [
        ...gpgArgs,
        ...armorArgs,
        "--export-secret-keys",
        serverUid,
      ]);
      writeFileSync(clientKeyPath, clientPrivateKey, { mode: 0o600 });
      writeFileSync(clientPublicKeyPath, clientPublicKey);
      writeFileSync(serverKeyPath, serverPrivateKey, { mode: 0o600 });

      const server = new ServerFixture(getRandomPort(), [
        "--key",
        serverKeyPath,
        "--client-keys",
        clientPublicKeyPath,
      ]);
      try {
        try {
          await server.start();
        } catch (error) {
          const fixtureLog = join(
            process.cwd(),
            `.server-${server.getPort()}.log`,
          );
          try {
            console.error(
              "[gpg-client-server-startup]",
              readFileSync(fixtureLog, "utf8"),
            );
          } catch {
            // The process may exit before the fixture log is created.
          }
          throw error;
        }
        await page.goto(
          `/client?serverUrl=${encodeURIComponent(server.getUrl())}`,
        );
        await page
          .getByRole("button", { name: "Crypto" })
          .click({ force: true });
        await page
          .getByRole("button", { name: "＋ Add private key" })
          .click({ force: true });
        if (keyFormat === "armored") {
          await page
            .getByLabel("Private key text")
            .fill(clientPrivateKey.toString("utf8"));
        } else {
          await page.getByLabel("Private key file").setInputFiles({
            name: `client-secret.${keyExtension}`,
            mimeType: "application/octet-stream",
            buffer: clientPrivateKey,
          });
        }
        await page
          .getByRole("button", { name: "Add to list" })
          .click({ force: true });
        await expectKeyActive(
          page,
          keyFormat === "binary" ? "client-secret.gpg" : "Identity 1",
        );

        await page
          .getByRole("button", { name: "＋ Add verification key" })
          .click({ force: true });
        if (keyFormat === "armored") {
          await page
            .getByLabel("Verification key text")
            .fill(serverPublicKey.toString("utf8"));
        } else {
          await page.getByLabel("Verification key file").setInputFiles({
            name: `server-public.${keyExtension}`,
            mimeType: "application/octet-stream",
            buffer: serverPublicKey,
          });
        }
        await page
          .getByRole("button", { name: "Add to list" })
          .click({ force: true });
        await expectKeyActive(
          page,
          keyFormat === "binary" ? "server-public.gpg" : "Verification key 1",
        );
        await page
          .getByRole("button", { name: "Save security settings" })
          .click({ force: true });

        await page.goto(
          `/client?test2&test&testServerUrl=${encodeURIComponent(server.getUrl())}`,
        );
        await expect(page.locator(".status")).toHaveText("Connected", {
          timeout: 45_000,
        });
        await expectClientRendersChangingFrames(page);
        const serverLogPath = `C:\\tmp\\ascii-chat-server-${server.getPort()}.log`;
        await expect
          .poll(
            () => {
              try {
                const logs = readFileSync(serverLogPath, "utf8");
                return (
                  logs.includes(
                    "Client key authentication successful (whitelist verified)",
                  ) && logs.includes("mutual authentication")
                );
              } catch {
                return false;
              }
            },
            { timeout: 10_000 },
          )
          .toBe(true);
      } finally {
        await server.stop();
      }
    } finally {
      rmSync(keyDir, { recursive: true, force: true });
    }
  });
}

test("Client rejects a valid identity that the server did not authorize", async ({
  page,
}) => {
  test.setTimeout(90_000);
  const keyDir = mkdtempSync(
    join(tmpdir(), "ascii-chat-unauthorized-identity-"),
  );
  const authorizedKeyPath = join(keyDir, "authorized-identity");
  const rejectedKeyPath = join(keyDir, "rejected-identity");
  const serverKeyPath = join(keyDir, "server-identity");
  execFileSync("ssh-keygen", [
    "-q",
    "-t",
    "ed25519",
    "-N",
    "",
    "-f",
    authorizedKeyPath,
  ]);
  execFileSync("ssh-keygen", [
    "-q",
    "-t",
    "ed25519",
    "-N",
    "",
    "-f",
    rejectedKeyPath,
  ]);
  execFileSync("ssh-keygen", [
    "-q",
    "-t",
    "ed25519",
    "-N",
    "",
    "-f",
    serverKeyPath,
  ]);
  const server = new ServerFixture(getRandomPort(), [
    "--key",
    serverKeyPath,
    "--client-keys",
    `${authorizedKeyPath}.pub`,
  ]);

  try {
    await server.start();
    await page.goto(`/client?serverUrl=${encodeURIComponent(server.getUrl())}`);
    await page.getByRole("button", { name: "Crypto" }).click({ force: true });
    await page
      .getByRole("button", { name: "＋ Add private key" })
      .click({ force: true });
    await page
      .getByLabel("Private key text")
      .fill(readFileSync(rejectedKeyPath, "utf8"));
    await page
      .getByRole("button", { name: "Add to list" })
      .click({ force: true });
    await expectKeyActive(page, "Identity 1");
    await page
      .getByRole("button", { name: "＋ Add verification key" })
      .click({ force: true });
    await page
      .getByLabel("Verification key text")
      .fill(readFileSync(`${serverKeyPath}.pub`, "utf8"));
    await page
      .getByRole("button", { name: "Add to list" })
      .click({ force: true });
    await expectKeyActive(page, "Verification key 1");
    await page
      .getByRole("button", { name: "Save security settings" })
      .click({ force: true });

    await page
      .getByRole("button", { name: "Connect", exact: true })
      .click({ force: true });
    const logPath = `C:\\tmp\\ascii-chat-server-${server.getPort()}.log`;
    await expect
      .poll(
        () => {
          try {
            return readFileSync(logPath, "utf8").includes(
              "Client Ed25519 key not in whitelist",
            );
          } catch {
            return false;
          }
        },
        { timeout: 30_000 },
      )
      .toBe(true);
    await expect(page.locator(".status")).not.toHaveText("Connected");
  } finally {
    await server.stop();
    rmSync(keyDir, { recursive: true, force: true });
  }
});

test("Discovery authenticates and verifies a real custom-crypto ACDS handshake", async ({
  page,
}) => {
  test.setTimeout(60_000);
  const keyDir = mkdtempSync(join(tmpdir(), "ascii-chat-discovery-crypto-"));
  const clientKeyPath = join(keyDir, "client-identity");
  const serverKeyPath = join(keyDir, "server-identity");
  const databasePath = join(keyDir, "acds.sqlite");
  const logPath = join(keyDir, "acds.log");
  const ports = await unusedTcpPorts(2);
  const tcpPort = ports[0]!;
  const websocketPort = ports[1]!;
  execFileSync("ssh-keygen", [
    "-q",
    "-t",
    "ed25519",
    "-N",
    "",
    "-f",
    clientKeyPath,
  ]);
  execFileSync("ssh-keygen", [
    "-q",
    "-t",
    "ed25519",
    "-N",
    "",
    "-f",
    serverKeyPath,
  ]);

  let service: ChildProcess | undefined;
  let serviceLogs = "";
  const browserLogs: string[] = [];
  try {
    page.on("console", (message) =>
      browserLogs.push(`[${message.type()}] ${message.text()}`),
    );
    page.on("pageerror", (error) =>
      browserLogs.push(`[pageerror] ${error.stack || error.message}`),
    );
    service = spawn(
      getAsciiChatBinaryPath(),
      [
        "--log-level",
        "debug",
        "--log-file",
        logPath,
        "discovery-service",
        "--port",
        String(tcpPort),
        "--websocket-port",
        String(websocketPort),
        "--database",
        databasePath,
        "--key",
        serverKeyPath,
        "--require-client-identity",
      ],
      { windowsHide: true, stdio: ["ignore", "pipe", "pipe"] },
    );
    for (const stream of [service.stdout, service.stderr]) {
      stream?.on("data", (chunk: Buffer) => {
        serviceLogs += chunk.toString();
      });
    }
    await waitForPort(tcpPort, "127.0.0.1");
    await waitForPort(websocketPort, "127.0.0.1");

    const query = new URLSearchParams({
      session: "crypto-browser-test",
      signalingUrl: `ws://127.0.0.1:${websocketPort}`,
    });
    await page.goto(`/discovery?${query}`);
    await page.getByRole("button", { name: "Crypto" }).click({ force: true });
    await page
      .getByRole("button", { name: "＋ Add private key" })
      .click({ force: true });
    await page
      .getByLabel("Private key text")
      .fill(readFileSync(clientKeyPath, "utf8"));
    await page
      .getByRole("button", { name: "Add to list" })
      .click({ force: true });
    await expectKeyActive(page, "Identity 1");
    await page
      .getByRole("button", { name: "＋ Add verification key" })
      .click({ force: true });
    await page
      .getByLabel("Verification key target")
      .selectOption("discovery-service");
    await page
      .getByLabel("Verification key text")
      .fill(readFileSync(`${serverKeyPath}.pub`, "utf8"));
    await page
      .getByRole("button", { name: "Add to list" })
      .click({ force: true });
    await expectKeyActive(page, "Verification key 1");
    await page
      .getByRole("button", { name: "Save security settings" })
      .click({ force: true });
    await page
      .getByRole("button", { name: "Join session" })
      .click({ force: true });

    let handshakeOutcome = "pending";
    try {
      await expect
        .poll(
          () => {
            try {
              serviceLogs = `${serviceLogs}\n${readFileSync(logPath, "utf8")}`;
            } catch {
              // The service log is created after its first logging call.
            }
            if (
              serviceLogs.includes(
                "Crypto handshake completed for WebSocket client",
              )
            ) {
              handshakeOutcome = "completed";
              return "completed";
            }
            if (
              serviceLogs.includes(
                "Crypto handshake failed for WebSocket client",
              )
            ) {
              handshakeOutcome = "failed";
              return "failed";
            }
            return "pending";
          },
          { timeout: 30_000 },
        )
        .toMatch(/completed|failed/);
    } catch (error) {
      const recent = serviceLogs
        .split(/\r?\n/)
        .filter((line) =>
          /handshake|identity|verification|auth response|CRYPTO_|signature|failed|rejected/i.test(
            line,
          ),
        )
        .slice(-25)
        .join("\n");
      console.error(
        "[discovery-crypto-diagnostics]",
        recent || serviceLogs.slice(-8000),
      );
      console.error(
        "[discovery-crypto-page]",
        await page.locator("body").innerText(),
      );
      console.error(
        "[discovery-crypto-browser]",
        browserLogs.slice(-60).join("\n"),
      );
      throw error;
    }
    const handshakeDetails = serviceLogs
      .split(/\r?\n/)
      .filter((line) =>
        /handshake|identity|verification|auth|failed|error/i.test(line),
      )
      .slice(-40)
      .join("\n");
    expect(handshakeOutcome, handshakeDetails).toBe("completed");
  } finally {
    if (service && service.exitCode === null) {
      await new Promise<void>((resolve) => {
        const timer = setTimeout(() => {
          service?.kill("SIGKILL");
          resolve();
        }, 5000);
        service?.once("exit", () => {
          clearTimeout(timer);
          resolve();
        });
        service?.kill("SIGTERM");
      });
    }
    rmSync(keyDir, { recursive: true, force: true });
  }
});
