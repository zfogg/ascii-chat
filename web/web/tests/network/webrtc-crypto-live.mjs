// Real browser DataChannels against an isolated native host and discovery service.
import { chromium } from "@playwright/test";
import { spawn, execFileSync } from "node:child_process";
import { mkdtempSync, readFileSync, existsSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { createServer } from "node:net";
import assert from "node:assert/strict";
const binary = resolve(process.argv[2]);
const url = process.argv[3] || "http://127.0.0.1:30831";
const directory = mkdtempSync(join(tmpdir(), "ascii-webrtc-security-"));
console.log(directory);
const env = Object.fromEntries(
  Object.entries(process.env).filter(([k]) => !k.startsWith("ASCII_CHAT_")),
);
Object.assign(env, {
  CLAUDECODE: "1",
  HOME: directory,
  USERPROFILE: directory,
  APPDATA: directory,
  XDG_CONFIG_HOME: directory,
  ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK: "1",
});
async function port() {
  const s = createServer();
  await new Promise((r) => s.listen(0, "127.0.0.1", r));
  const p = s.address().port;
  await new Promise((r) => s.close(r));
  return p;
}
async function wait(check, ms = 30000) {
  const deadline = Date.now() + ms;
  while (Date.now() < deadline) {
    const v = check();
    if (v) return v;
    await new Promise((r) => setTimeout(r, 50));
  }
  throw Error("Timed out waiting for native service");
}
function launch(name, args) {
  const log = join(directory, name + ".log");
  const child = spawn(
    binary,
    ["--log-file", log, "--log-level", "debug", ...args],
    { cwd: directory, env, windowsHide: true, stdio: "ignore" },
  );
  return {
    child,
    text: () => (existsSync(log) ? readFileSync(log, "utf8") : ""),
  };
}
for (const key of ["host", "wrong"])
  execFileSync("ssh-keygen", [
    "-q",
    "-t",
    "ed25519",
    "-N",
    "",
    "-f",
    join(directory, key),
  ]);
const pin = readFileSync(join(directory, "host.pub"), "utf8"),
  wrong = readFileSync(join(directory, "wrong.pub"), "utf8");
const acdsPort = await port(),
  acdsWs = await port();
const acds = launch("acds", [
  "discovery-service",
  "127.0.0.1",
  "--key",
  join(directory, "host"),
  "--port",
  String(acdsPort),
  "--websocket-port",
  String(acdsWs),
  "--database",
  join(directory, "acds.db"),
  "--status-screen=false",
]);
const browser = await chromium.launch({ headless: true });
try {
  await wait(() => {
    if (acds.child.exitCode !== null) throw Error(acds.text());
    return acds.text().includes("Server accepting connections");
  });
  const cases = [
    ["default", [], {}, undefined, 0, true],
    ["explicit", [], {}, true, 1, true],
    [
      "password",
      ["--password", "test-password"],
      { password: "test-password" },
      undefined,
      3,
      true,
    ],
    [
      "password-auth-only",
      ["--password", "test-password"],
      { password: "test-password" },
      false,
      2,
      true,
    ],
    [
      "key-auth-only",
      ["--key", join(directory, "host")],
      { expectedServerPublicKeyText: pin },
      false,
      2,
      true,
    ],
    [
      "wrong-password",
      ["--password", "test-password"],
      { password: "wrong-password" },
      undefined,
      3,
      false,
    ],
    [
      "wrong-pin",
      ["--key", join(directory, "host")],
      { expectedServerPublicKeyText: wrong },
      false,
      2,
      false,
    ],
  ];
  for (const [
    name,
    flags,
    cryptoOptions,
    applicationEncryption,
    mode,
    ok,
  ] of cases) {
    const host = launch(name, [
      "server",
      "0.0.0.0",
      "--port",
      String(await port()),
      "--websocket-port",
      String(await port()),
      "--status-screen=false",
      "--discovery",
      "--webrtc",
      "--discovery-service",
      "127.0.0.1",
      "--discovery-service-port",
      String(acdsPort),
      ...(flags.includes("--key") ? [] : ["--key", join(directory, "host")]),
      ...flags,
    ]);
    const context = await browser.newContext();
    const page = await context.newPage();
    try {
      const sessionName = await wait(
        () => host.text().match(/Session created: ([a-z]+-[a-z]+-[a-z]+)/)?.[1],
      );
      await page.goto(url + "/tests/network/crypto-policy.html");
      await page.waitForFunction(() => !!window.runSecurityTest);
      const result = await page.evaluate(
        (opts) => window.runSecurityTest(opts),
        {
          sessionName,
          password: flags.includes("--password") ? "test-password" : "",
          signalingUrl: `ws://127.0.0.1:${acdsWs}`,
          stunServers: [],
          turnServers: [],
          cryptoOptions,
          applicationEncryption,
        },
      );
      console.log(name, JSON.stringify(result));
      assert.equal(result.ok, ok, name);
      if (ok) assert.equal(result.encrypted, !!(mode & 1), name);
      else {
        assert(!result.states.includes(3), name);
        assert(!result.error.includes("timed out"), result.error);
      }
    } finally {
      await context.close();
      host.child.kill();
    }
  }
  for (const [
    name,
    hostCrypto,
    clientCrypto,
    encryption,
    encrypted,
    accepted,
  ] of [
    ["default", {}, {}, undefined, false, true],
    ["explicit", {}, {}, true, true, true],
    [
      "password",
      { password: "test-password" },
      { password: "test-password" },
      undefined,
      true,
      true,
    ],
    [
      "auth-only",
      { password: "test-password" },
      { password: "test-password" },
      false,
      false,
      true,
    ],
    [
      "wrong-password",
      { password: "test-password" },
      { password: "wrong-password" },
      undefined,
      true,
      false,
    ],
    [
      "key-auth-only",
      { identityPrivateKeyText: readFileSync(join(directory, "host"), "utf8") },
      { expectedServerPublicKeyText: pin },
      false,
      false,
      true,
    ],
  ]) {
    const hostContext = await browser.newContext(),
      clientContext = await browser.newContext();
    try {
      const hp = await hostContext.newPage(),
        cp = await clientContext.newPage();
      await hp.goto(url + "/tests/network/crypto-policy.html");
      await hp.waitForFunction(() => !!window.startBrowserHost);
      const common = {
        password: "",
        sessionName: "",
        signalingUrl: `ws://127.0.0.1:${acdsWs}`,
        stunServers: [],
        turnServers: [],
      };
      const sessionName = await hp.evaluate((o) => window.startBrowserHost(o), {
        ...common,
        cryptoOptions: hostCrypto,
        applicationEncryption: encryption,
      });
      await cp.goto(url + "/tests/network/crypto-policy.html");
      await cp.waitForFunction(() => !!window.runSecurityTest);
      const result = await cp.evaluate((o) => window.runSecurityTest(o), {
        ...common,
        sessionName,
        cryptoOptions: clientCrypto,
        applicationEncryption: encryption,
      });
      console.log("browser-host-" + name, JSON.stringify(result));
      assert.equal(result.ok, accepted, name);
      if (accepted) assert.equal(result.encrypted, encrypted, name);
      else {
        assert(!result.error.includes("timed out"), result.error);
        assert.equal(
          await hp.evaluate(() => window.testHost.getState()),
          3,
          "Rejected peer stopped host",
        );
        await cp.reload();
        await cp.waitForFunction(() => !!window.runSecurityTest);
        const retry = await cp.evaluate((o) => window.runSecurityTest(o), {
          ...common,
          sessionName,
          cryptoOptions: hostCrypto,
          applicationEncryption: encryption,
        });
        assert(retry.ok, "Valid peer could not join after rejection");
      }
    } finally {
      await clientContext.close();
      await hostContext.close();
    }
  }
} finally {
  await browser.close();
  acds.child.kill();
}
