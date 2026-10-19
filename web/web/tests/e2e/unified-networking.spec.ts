import { expect, test, type Page, type TestInfo } from "@playwright/test";
import { spawn, type ChildProcess } from "node:child_process";
import { createWriteStream, mkdirSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";
import { createServer } from "node:net";

test.beforeEach(async ({ page }) => {
  // Seed suggestions from the separate website API for this isolated dev server.
  await page.route("**/api/session-strings?**", (route) =>
    route.fulfill({
      headers: { "access-control-allow-origin": "*" },
      json: { strings: ["blue-mountain-tiger"] },
    }),
  );
});

async function port(): Promise<number> {
  const server = createServer();
  await new Promise<void>((done) => server.listen(0, "127.0.0.1", done));
  const value = (server.address() as { port: number }).port;
  await new Promise<void>((done, reject) =>
    server.close((error) => (error ? reject(error) : done())),
  );
  return value;
}
function observeConnectionErrors(page: Page): string[] {
  const errors: string[] = [];
  page.on("pageerror", (error) => errors.push(error.message));
  page.on("console", (message) => {
    if (
      message.type() === "error" &&
      /Options not initialized|Security negotiation is not complete/.test(
        message.text(),
      )
    )
      errors.push(message.text());
  });
  return errors;
}
class Native {
  process: ChildProcess;
  output = "";
  log: ReturnType<typeof createWriteStream>;
  closed: Promise<void>;
  constructor(info: TestInfo, name: string, args: string[]) {
    const binary =
      process.env.ASCII_CHAT_TEST_BINARY ||
      resolve("../../build/bin/ascii-chat.exe");
    this.log = createWriteStream(info.outputPath(`${name}.log`));
    const home = info.outputPath("config");
    mkdirSync(home, { recursive: true });
    this.process = spawn(
      binary,
      [
        "--no-check-update",
        "--log-level",
        process.env.ASCII_CHAT_TEST_LOG_LEVEL || "info",
        ...args,
      ],
      {
        windowsHide: true,
        env: {
          ...process.env,
          HOME: home,
          USERPROFILE: home,
          APPDATA: home,
          XDG_CONFIG_HOME: home,
          ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK: "1",
          ASCII_CHAT_QUESTION_PROMPT_RESPONSE: "y;y;y",
        },
      },
    );
    this.closed = new Promise<void>((done) =>
      this.process.once("close", () => {
        this.log.end();
        done();
      }),
    );
    for (const stream of [this.process.stdout, this.process.stderr])
      stream?.on("data", (data: Buffer) => {
        this.output += data.toString();
        this.log.write(data);
      });
    this.process.on("error", (error) => {
      this.output += error.stack;
    });
  }
  async ready(pattern: RegExp) {
    await expect.poll(() => this.output, { timeout: 30_000 }).toMatch(pattern);
    expect(this.process.exitCode).toBeNull();
  }
  async stop() {
    if (
      this.process.pid &&
      this.process.exitCode === null &&
      this.process.signalCode === null
    ) {
      this.process.kill();
    }
    await this.closed;
    expect(this.output).not.toMatch(/AddressSanitizer|EXCEPTION CAUGHT/);
  }
}
async function rendered(page: Page, info: TestInfo, name: string) {
  const frames = () =>
    page.evaluate(() => window.__clientFrameMetrics?.rendered ?? 0);
  const initial = await frames();
  await expect.poll(frames, { timeout: 25_000 }).toBeGreaterThan(initial + 10);
  const before = await frames();
  const canvas = page.locator("canvas.ascii-canvas");
  const imageBefore = await canvas.evaluate((element) =>
    (element as HTMLCanvasElement).toDataURL(),
  );
  await page.waitForTimeout(5_000);
  const delta = (await frames()) - before;
  expect(delta).toBeGreaterThan(100);
  expect(
    await canvas.evaluate((element) =>
      (element as HTMLCanvasElement).toDataURL(),
    ),
  ).not.toBe(imageBefore);
  console.log(`${name}: ${delta} rendered frames in five seconds`);
  await page.screenshot({
    path: info.outputPath(`${name}.png`),
    fullPage: true,
  });
}

test("/client renders a native WebSocket session and reconnects", async ({
  page,
}, info) => {
  const tcp = await port(),
    ws = await port();
  const server = new Native(info, "server", [
    "server",
    "127.0.0.1",
    "--port",
    String(tcp),
    "--websocket-port",
    String(ws),
    "--status-screen=false",
  ]);
  let producer: Native | undefined;
  const errors = observeConnectionErrors(page);
  try {
    await server.ready(/TCP event backend:/);
    producer = new Native(info, "producer", [
      "client",
      `tcp://127.0.0.1:${tcp}`,
      "--test-pattern",
      "--fps",
      "60",
      "--width",
      "80",
      "--height",
      "24",
      "--audio=false",
      "--splash-screen=false",
      "--video-codec",
      "raw",
    ]);
    await page.goto(
      `/client?serverUrl=${encodeURIComponent(`ws://127.0.0.1:${ws}`)}`,
    );
    for (let cycle = 0; cycle < 2; ++cycle) {
      await page.getByRole("button", { name: "Connect", exact: true }).click();
      await expect(page.locator(".status")).toHaveText("Connected", {
        timeout: 25_000,
      });
      await rendered(page, info, `client-${cycle}`);
      await page
        .getByRole("button", { name: "Disconnect", exact: true })
        .click();
      await expect(
        page.getByRole("button", { name: "Connect", exact: true }),
      ).toBeVisible();
      await page.setViewportSize({ width: 1260 + cycle * 20, height: 880 });
      await page.waitForTimeout(250);
    }
    expect(errors).toEqual([]);
  } finally {
    await producer?.stop();
    await server.stop();
  }
});

test("/discovery joins native ACDS over WebSocket and renders an authenticated WebRTC session", async ({
  page,
}, info) => {
  const tcp = await port(),
    ws = await port(),
    acdsTcp = await port(),
    acdsWs = await port();
  // Public RFC 8032 vector, used only by this isolated fixture.
  const identity = info.outputPath("acds-test-key");
  writeFileSync(
    identity,
    Buffer.from(
      "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60" +
        "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
      "hex",
    ),
  );
  const acds = new Native(info, "acds", [
    "discovery-service",
    "127.0.0.1",
    "--key",
    identity,
    "--port",
    String(acdsTcp),
    "--websocket-port",
    String(acdsWs),
    "--database",
    info.outputPath("acds.db"),
    "--status-screen=false",
  ]);
  let server: Native | undefined, producer: Native | undefined;
  const errors = observeConnectionErrors(page);
  try {
    await acds.ready(/TCP event backend:/);
    server = new Native(info, "host", [
      "server",
      "0.0.0.0",
      "--port",
      String(tcp),
      "--websocket-port",
      String(ws),
      "--password",
      "network-test-password",
      "--discovery",
      "--webrtc",
      "--discovery-service",
      "127.0.0.1",
      "--discovery-service-port",
      String(acdsTcp),
      "--status-screen=false",
    ]);
    await server.ready(/Session created: [a-z]+-[a-z]+-[a-z]+/);
    const session = server.output.match(
      /Session created: ([a-z]+-[a-z]+-[a-z]+)/,
    )![1]!;
    producer = new Native(info, "producer", [
      "client",
      `tcp://127.0.0.1:${tcp}`,
      "--password",
      "network-test-password",
      "--test-pattern",
      "--fps",
      "60",
      "--width",
      "80",
      "--height",
      "24",
      "--audio=false",
      "--splash-screen=false",
    ]);
    await page.goto("/discovery");
    await page.getByLabel("Session name", { exact: true }).fill(session);
    await page.getByText("Connection settings", { exact: true }).click();
    await page
      .getByLabel("Discovery service URL")
      .fill(`ws://127.0.0.1:${acdsWs}`);
    await page
      .getByLabel("Session password", { exact: true })
      .fill("network-test-password");
    await page.getByLabel("STUN URLs").fill("");
    await page.getByLabel("TURN URLs").fill("");
    await page
      .getByRole("button", { name: "Join session", exact: true })
      .click();
    await expect(
      page.getByRole("button", { name: "Disconnect", exact: true }),
    ).toBeVisible({ timeout: 40_000 });
    await rendered(page, info, "discovery");
    await server.ready(/Authenticated WebRTC connection adopted/);
    await page.getByRole("button", { name: "Disconnect", exact: true }).click();
    await page.setViewportSize({ width: 1260, height: 880 });
    await page.waitForTimeout(250);
    await expect(
      page.getByRole("button", { name: "Join session", exact: true }),
    ).toBeVisible();
    expect(errors).toEqual([]);
  } finally {
    await producer?.stop();
    await server?.stop();
    await acds.stop();
  }
});
