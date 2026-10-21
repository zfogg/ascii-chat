import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import fs from "node:fs";
import net from "node:net";
import path from "node:path";
import { pathToFileURL } from "node:url";

// Run in a Linux environment with the native binary, Chromium, and playwright-core.
// Both native processes and browser pages use real ACDS and WebRTC transports.
const { chromium } = await import(
  pathToFileURL(
    process.env.PLAYWRIGHT_CORE_PATH ||
      "/app/web/node_modules/.pnpm/playwright-core@1.58.2/node_modules/playwright-core/index.mjs",
  ).href
);
const binary =
  process.env.ASCII_CHAT_TEST_BINARY || "/work/build/bin/ascii-chat";
const website =
  process.env.HANDOFF_WEB_URL || "http://host.docker.internal:3080";
const directory = process.env.HANDOFF_ARTIFACTS || "/app/.tmp/handoff-e2e";
fs.mkdirSync(directory, { recursive: true });
const delay = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
async function until(read, timeout = 30000, description = "condition") {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) {
    const value = await bounded(
      read,
      Math.max(1, deadline - Date.now()),
      description,
    );
    if (value) return value;
    await delay(100);
  }
  throw new Error(`Timed out waiting for ${description}`);
}
async function bounded(read, timeout = 30000, description = "operation") {
  let timer;
  try {
    return await Promise.race([
      Promise.resolve().then(read),
      new Promise((_, reject) => {
        timer = setTimeout(
          () => reject(new Error("Timed out waiting for " + description)),
          timeout,
        );
      }),
    ]);
  } finally {
    clearTimeout(timer);
  }
}
let ordinal = 0;
function native(args, name) {
  const log = path.join(directory, `${name}.log`);
  fs.writeFileSync(log, "");
  const child = spawn(
    binary,
    [
      "--log-level",
      process.env.HANDOFF_LOG_LEVEL || "info",
      "--log-file",
      log,
      ...args,
    ],
    {
      env: {
        ...process.env,
        ASAN_OPTIONS: "detect_leaks=0",
        CLAUDECODE: "1",
        TERM: "xterm-256color",
      },
      stdio: ["ignore", "ignore", "pipe"],
    },
  );
  child.stderr.on("data", (bytes) => {
    fs.appendFileSync(`${log}.stderr`, bytes);
  });
  child.on("exit", (code, signal) =>
    fs.writeFileSync(`${log}.exit.json`, JSON.stringify({ code, signal })),
  );
  const read = () =>
    // Keep offsets chronological: stderr is archived separately and can repeat
    // the same messages. Concatenating it shifts old entries past saved offsets.
    fs.existsSync(log) ? fs.readFileSync(log, "utf8") : "";
  return { child, read, log };
}
const launch = {
  executablePath: process.env.CHROMIUM_PATH || "/usr/sbin/chromium",
  headless: true,
  args: [
    "--no-sandbox",
    "--disable-dev-shm-usage",
    "--autoplay-policy=no-user-gesture-required",
    "--disable-background-timer-throttling",
    "--disable-renderer-backgrounding",
    `--unsafely-treat-insecure-origin-as-secure=${website}`,
  ],
};
async function page(browser, name) {
  const context = await browser.newContext();
  const result = await context.newPage();
  const output = fs.createWriteStream(
    path.join(directory, `${name}-browser.log`),
  );
  result.on("console", (event) =>
    output.write(`${event.type()} ${event.text()}\n`),
  );
  result.on("pageerror", (error) =>
    output.write(`PAGE ERROR ${error.stack}\n`),
  );
  result.on("close", () => output.end());
  await result.goto(`${website}/tests/e2e/handoff-probe.html?test2`);
  await result.waitForFunction(() => Boolean(window.handoffProbe));
  return result;
}
async function startPage(page, create, name, port) {
  await page.evaluate(
    ({ create, name, port }) =>
      window.handoffProbe.start(create, name, `ws://127.0.0.1:${port}`),
    { create, name, port },
  );
}
async function media(page, previous = 0) {
  await until(
    () =>
      page.evaluate(
        (previous) => window.handoffProbe.unique >= previous + 5,
        previous,
      ),
    30000,
    "five new decoded ASCII images",
  );
  assert((await page.locator("#media").innerText()).trim().length > 5);
}
function peerArgs(name, port, listen) {
  return [
    ...(name ? [name] : []),
    "--discovery-service",
    "127.0.0.1",
    "--discovery-service-port",
    String(port),
    "--port",
    String(listen),
    "--test-pattern",
    "--width",
    "40",
    "--height",
    "16",
    "--fps",
    "20",
    "--audio=false",
    "--color-mode",
    "none",
    "--stun-servers",
    "",
    "--turn-servers",
    "",
  ];
}
async function throttledProxy(port, target) {
  const sockets = new Set();
  const server = net.createServer((incoming) => {
    const outgoing = net.connect(target, "127.0.0.1");
    sockets.add(incoming);
    sockets.add(outgoing);
    incoming.on("error", () => outgoing.destroy());
    outgoing.on("error", () => incoming.destroy());
    incoming.on("close", () => {
      sockets.delete(incoming);
      outgoing.destroy();
    });
    outgoing.on("close", () => {
      sockets.delete(outgoing);
      incoming.destroy();
    });
    outgoing.pipe(incoming);
    void (async () => {
      try {
        for await (const bytes of incoming) {
          for (let offset = 0; offset < bytes.length; offset += 1024) {
            await delay(10);
            if (outgoing.destroyed) return;
            outgoing.write(bytes.subarray(offset, offset + 1024));
          }
        }
      } catch {
        outgoing.destroy();
      }
    })();
  });
  await new Promise((resolve) => server.listen(port, "127.0.0.1", resolve));
  return {
    close() {
      for (const socket of sockets) socket.destroy();
      server.close();
    },
  };
}
async function scenario(kind) {
  const index = ordinal++,
    base = 28100 + index * 20;
  const processes = [];
  let browser, hostBrowser, hostServer, proxy;
  const acds = native(
    [
      "discovery-service",
      "--port",
      String(base),
      "--websocket-port",
      String(base + 1),
      "--database",
      path.join(directory, `${kind}.db`),
      "--status-screen=false",
    ],
    `${kind}-acds`,
  );
  processes.push(acds);
  try {
    await until(
      () =>
        new Promise((resolve) => {
          const socket = net.connect(base, "127.0.0.1");
          socket.on("connect", () => {
            socket.destroy();
            resolve(true);
          });
          socket.on("error", () => resolve(false));
        }),
      15000,
      "ACDS listener",
    );
    if (kind !== "native") browser = await chromium.launch(launch);
    let name, host;
    if (kind === "browser") {
      hostServer = await chromium.launchServer(launch);
      hostBrowser = await chromium.connect(hostServer.wsEndpoint());
      host = await page(hostBrowser, `${kind}-host`);
      await startPage(host, true, "", base + 1);
      name = await until(
        () => host.evaluate(() => window.handoffProbe.name),
        15000,
        "created browser session",
      );
    } else {
      host = native(peerArgs("", base, base + 2), `${kind}-host`);
      processes.push(host);
      name = await until(
        () =>
          host
            .read()
            .match(/Share this string to connect: ([a-z]+-[a-z]+-[a-z]+)/)?.[1],
        30000,
        "created native session",
      );
    }
    if (kind === "native") {
      const a = native(peerArgs(name, base, base + 3), `${kind}-a`);
      processes.push(a);
      proxy = await throttledProxy(base + 10, base);
      const b = native(peerArgs(name, base + 10, base + 4), `${kind}-b`);
      processes.push(b);
      const expectedId = await until(
        () => a.read().match(/Handoff identity=([0-9a-f]{8})/)?.[1],
      );
      await until(
        () =>
          a.read().includes(`Prepared successor ${expectedId}`) &&
          b.read().includes(`Prepared successor ${expectedId}`),
        30000,
        "successor acknowledged by both native peers",
      );
      await until(
        () =>
          /rendered=[1-9]/.test(a.read()) && /rendered=[1-9]/.test(b.read()),
        30000,
        "native media before host death",
      );
      const offsets = [a.read().length, b.read().length];
      host.child.kill("SIGKILL");
      await until(
        () =>
          /Committed successor/.test(a.read().slice(offsets[0])) &&
          /Committed successor/.test(b.read().slice(offsets[1])),
        20000,
        "committed native successor",
      );
      await until(
        () =>
          /Hosting session/.test(
            a.read().slice(a.read().lastIndexOf("Committed successor")),
          ),
        30000,
        "elected native successor running its host media loop",
      );
      await until(
        () => {
          const resumed = b
            .read()
            .slice(b.read().lastIndexOf("Committed successor"));
          const active = resumed.lastIndexOf("Discovery state: ACTIVE");
          return active >= 0 && /rendered=[1-9]/.test(resumed.slice(active));
        },
        30000,
        "new native media after reconnecting to the successor",
      );
    } else if (kind === "mixed") {
      const a = await page(browser, "mixed-a");
      const b = await page(browser, "mixed-ui");
      proxy = await throttledProxy(base + 11, base + 1);
      await startPage(a, false, name, base + 1);
      await b.addInitScript(() => {
        window.handoffConnectedPeers = 0;
        const Original = window.RTCPeerConnection;
        window.RTCPeerConnection = class extends Original {
          constructor(...args) {
            super(...args);
            this.addEventListener("connectionstatechange", () => {
              if (this.connectionState === "connected")
                window.handoffConnectedPeers++;
            });
          }
        };
      });
      const query = new URLSearchParams({
        session: name,
        signalingUrl: `ws://127.0.0.1:${base + 11}`,
        stunUrls: "",
        turnUrls: "",
        test2: "",
      });
      await b.goto(`${website}/discovery?${query}`);
      await b
        .getByRole("button", { name: "Join session", exact: true })
        .click();
      await until(
        () =>
          b.evaluate(
            () => (window.__clientFrameMetrics?.changedReceived || 0) >= 5,
          ),
        30000,
        "React discovery page receiving changing ASCII images",
      );
      await media(a);
      const expectedId = await a.evaluate(
        () => window.handoffProbe.participant,
      );
      await until(
        () =>
          a.evaluate(
            (id) => window.handoffProbe.status === `Successor prepared: ${id}`,
            expectedId,
          ),
        20000,
        "expected mixed-session successor",
      );
      host.child.kill("SIGKILL");
      await until(
        () => a.evaluate(() => window.handoffProbe.migrations >= 1),
        20000,
        "mixed successor commit",
      );
      await until(
        () => b.evaluate(() => window.handoffConnectedPeers >= 2),
        30000,
        "React discovery page connected to successor's new peer connection",
      );
      const before = await b.evaluate(
        () => window.__clientFrameMetrics?.changedReceived || 0,
      );
      await until(
        () =>
          b.evaluate(
            (previous) =>
              (window.__clientFrameMetrics?.changedReceived || 0) >=
              previous + 5,
            before,
          ),
        30000,
        "React page receiving new images after successor reconnection",
      );
      await media(a, await a.evaluate(() => window.handoffProbe.unique));
      await b.screenshot({
        path: path.join(directory, "mixed-react-resumed.png"),
      });
      await a.screenshot({ path: path.join(directory, "mixed-resumed.png") });
    } else {
      const a = await page(browser, `${kind}-a`),
        b = await page(browser, `${kind}-b`);
      proxy = await throttledProxy(base + 11, base + 1);
      await startPage(a, false, name, base + 1);
      await startPage(b, false, name, base + 11);
      await media(a);
      await media(b);
      const expectedId = await a.evaluate(
        () => window.handoffProbe.participant,
      );
      await until(
        () =>
          a.evaluate(
            (id) => window.handoffProbe.status === `Successor prepared: ${id}`,
            expectedId,
          ),
        20000,
        "prepared browser successor",
      );
      const before = await Promise.all(
        [a, b].map((p) => p.evaluate(() => window.handoffProbe.unique)),
      );
      if (kind === "browser") hostServer.process().kill("SIGKILL");
      else host.child.kill("SIGKILL");
      await until(
        () => a.evaluate(() => window.handoffProbe.migrations >= 1),
        20000,
        "browser successor commit",
      );
      await until(
        () => b.evaluate(() => window.handoffProbe.migrations >= 1),
        20000,
        "browser peer commit",
      );
      const resumed = await Promise.all(
        [a, b].map((p) => p.evaluate(() => window.handoffProbe.unique)),
      );
      await media(a, Math.max(before[0], resumed[0]));
      await media(b, Math.max(before[1], resumed[1]));
      await a.screenshot({ path: path.join(directory, `${kind}-resumed.png`) });
      await b.screenshot({
        path: path.join(directory, `${kind}-peer-resumed.png`),
      });
    }
    const result = {
      scenario: kind,
      passed: true,
      session: name,
      completed: new Date().toISOString(),
    };
    fs.writeFileSync(
      path.join(directory, `${kind}-result.json`),
      JSON.stringify(result, null, 2),
    );
    console.log(JSON.stringify(result));
  } finally {
    await browser?.close();
    await hostBrowser?.close().catch(() => {});
    await hostServer?.close().catch(() => {});
    proxy?.close();
    for (const process of processes)
      if (process.child.exitCode === null) process.child.kill("SIGKILL");
  }
}
export {
  bounded,
  chromium,
  native,
  launch,
  page,
  startPage,
  media,
  until,
  delay,
  directory,
  peerArgs,
};

if (
  process.argv[1] &&
  import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href
)
  for (const kind of (
    process.env.HANDOFF_SCENARIOS || "native,browser,mixed"
  ).split(","))
    await scenario(kind);
