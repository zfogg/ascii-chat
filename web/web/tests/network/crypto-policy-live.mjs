// Run against a rebuilt native binary and the Vite server for this checkout:
// vp exec node tests/network/crypto-policy-live.mjs <binary> <vite-url> [openssl]
import { chromium } from "@playwright/test";
import { spawn, execFileSync } from "node:child_process";
import { mkdtempSync, readFileSync, existsSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { createServer } from "node:net";
import assert from "node:assert/strict";

const binary = resolve(process.argv[2]);
const vite = process.argv[3];
const openssl = process.argv[4] || "openssl";
const directory = mkdtempSync(join(tmpdir(), "ascii-browser-crypto-"));
const pause = (ms) => new Promise((done) => setTimeout(done, ms));
async function port() {
  const server = createServer();
  await new Promise((done) => server.listen(0, "127.0.0.1", done));
  const value = server.address().port;
  await new Promise((done) => server.close(done));
  return value;
}
for (const name of ["server", "client", "wrong"])
  execFileSync("ssh-keygen", [
    "-q",
    "-t",
    "ed25519",
    "-N",
    "",
    "-f",
    join(directory, name),
  ]);
execFileSync(
  openssl,
  [
    "req",
    "-x509",
    "-newkey",
    "rsa:2048",
    "-nodes",
    "-days",
    "1",
    "-subj",
    "/CN=localhost",
    "-keyout",
    join(directory, "tls.key"),
    "-out",
    join(directory, "tls.pem"),
  ],
  { stdio: "ignore" },
);
const identity = readFileSync(join(directory, "client"), "utf8");
const wrongIdentity = readFileSync(join(directory, "wrong"), "utf8");
const pin = readFileSync(join(directory, "server.pub"), "utf8");
const wrong = readFileSync(join(directory, "wrong.pub"), "utf8");
const keys = [
  "--key",
  join(directory, "server"),
  "--client-keys",
  join(directory, "client.pub"),
];
const cases = [
  {
    name: "discovery-ws-default",
    discovery: true,
    secure: false,
    encrypted: true,
  },
  {
    name: "discovery-wss-default",
    discovery: true,
    secure: true,
    encrypted: false,
  },
  {
    name: "discovery-ws-auth-rejected",
    discovery: true,
    secure: false,
    keys: true,
    encryption: false,
    reject: true,
    serverFlags: [],
  },
  {
    name: "discovery-ws-auth-permitted",
    discovery: true,
    secure: false,
    keys: true,
    encryption: false,
    encrypted: false,
    serverFlags: ["--no-encrypt"],
  },
  {
    name: "discovery-wss-explicit-required",
    discovery: true,
    secure: true,
    reject: true,
    serverFlags: ["--encrypt"],
  },
  {
    name: "discovery-wss-explicit-accepted",
    discovery: true,
    secure: true,
    encryption: true,
    encrypted: true,
    serverFlags: ["--encrypt"],
  },
  {
    name: "discovery-ws-auth-required",
    discovery: true,
    secure: false,
    keys: true,
    encrypted: true,
    serverFlags: ["--require-client-identity"],
  },
  {
    name: "discovery-ws-missing-auth",
    discovery: true,
    secure: false,
    reject: true,
    serverFlags: ["--require-client-identity"],
  },
  {
    name: "ws-second-key",
    secure: false,
    keys: true,
    secondKey: true,
    encrypted: true,
  },
  {
    name: "wss-second-key-auth-only",
    secure: true,
    keys: true,
    secondKey: true,
    encrypted: false,
  },
  { name: "ws-default", secure: false, encrypted: true },
  { name: "ws-none", secure: false, encryption: false, encrypted: false },
  { name: "wss-default", secure: true, encrypted: false },
  { name: "wss-explicit", secure: true, encryption: true, encrypted: true },
  {
    name: "wss-password",
    secure: true,
    password: "test-password",
    encrypted: true,
  },
  {
    name: "wss-password-auth-only",
    secure: true,
    password: "test-password",
    encryption: false,
    encrypted: false,
  },
  {
    name: "ws-auth-only",
    secure: false,
    encryption: false,
    keys: true,
    encrypted: false,
  },
  {
    name: "wss-auth-only",
    secure: true,
    encryption: false,
    keys: true,
    encrypted: false,
  },
  { name: "wss-key-default", secure: true, keys: true, encrypted: false },
  {
    name: "wss-wrong-identity",
    secure: true,
    keys: true,
    badIdentity: true,
    reject: true,
  },
  {
    name: "wss-wrong-pin",
    secure: true,
    keys: true,
    badPin: true,
    reject: true,
  },
  {
    name: "wss-wrong-password",
    secure: true,
    password: "test-password",
    badPassword: true,
    reject: true,
  },
];
const browser = await chromium.launch({
  args: ["--ignore-certificate-errors"],
});
try {
  for (const item of cases) {
    const tcp = await port(),
      ws = await port();
    const log = join(directory, `${item.name}.log`);
    const args = [
      "--log-file",
      log,
      "--log-level",
      "debug",
      item.discovery ? "discovery-service" : "server",
      "127.0.0.1",
      "--port",
      String(tcp),
      "--websocket-port",
      String(ws),
      "--status-screen=false",
      ...(item.secure
        ? [
            "--websocket-tls-cert",
            join(directory, "tls.pem"),
            "--websocket-tls-key",
            join(directory, "tls.key"),
          ]
        : []),
      ...(item.password ? ["--password", item.password] : []),
      ...(item.discovery && !item.keys
        ? ["--key", join(directory, "server")]
        : []),
      ...(item.secondKey ? ["--key", join(directory, "wrong")] : []),
      ...(item.keys ? (item.discovery ? keys.slice(0, 2) : keys) : []),
      ...(item.discovery
        ? ["--database", join(directory, item.name + ".db")]
        : []),
      ...(item.serverFlags ??
        (!item.secure && item.encryption === false ? ["--no-encrypt"] : [])),
    ];
    const env = Object.fromEntries(
      Object.entries(process.env).filter(
        ([key]) => !key.startsWith("ASCII_CHAT_"),
      ),
    );
    const server = spawn(binary, args, {
      cwd: directory,
      windowsHide: true,
      stdio: "ignore",
      env: {
        ...env,
        CLAUDECODE: "1",
        HOME: directory,
        USERPROFILE: directory,
        APPDATA: directory,
        XDG_CONFIG_HOME: directory,
      },
    });
    const context = await browser.newContext({ ignoreHTTPSErrors: true });
    try {
      let ready = false;
      for (let i = 0; i < 400; i++) {
        if (
          existsSync(log) &&
          readFileSync(log, "utf8").includes("Server accepting connections")
        ) {
          ready = true;
          break;
        }
        assert.equal(server.exitCode, null, `Server exited: ${log}`);
        await pause(100);
      }
      assert(ready, `Server did not start: ${log}`);
      const page = await context.newPage();
      page.on("pageerror", (error) => console.error(error));
      await page.goto(`${vite}/tests/network/crypto-policy.html`);
      await page.waitForFunction(
        () => typeof window.runSecurityTest === "function",
      );
      const options = {
        serverUrl: `${item.secure ? "wss" : "ws"}://127.0.0.1:${ws}`,
        ...(item.encryption === undefined
          ? {}
          : { applicationEncryption: item.encryption }),
        discoveryHandshake: !!item.discovery,
        cryptoOptions: {
          ...(item.password
            ? { password: item.badPassword ? "wrong-password" : item.password }
            : {}),
          ...(item.keys
            ? {
                identityPrivateKeyText: item.badIdentity
                  ? wrongIdentity
                  : identity,
                expectedServerPublicKeyText: item.badPin ? wrong : pin,
              }
            : {}),
        },
      };
      const result = await page.evaluate(
        (value) => window.runSecurityTest(value),
        options,
      );
      assert.equal(
        result.ok,
        !item.reject,
        `${item.name}: ${JSON.stringify(result)}; log: ${log}`,
      );
      if (!item.reject)
        assert.equal(result.encrypted, item.encrypted, item.name);
      else {
        assert(
          item.discovery || !result.states.includes(3),
          "Authentication failure reported CONNECTED",
        );
        assert(
          !result.error.includes("timed out"),
          "Expected explicit authentication rejection",
        );
      }
      console.log(`PASS ${item.name}: ${JSON.stringify(result)}`);
    } finally {
      await context.close();
      server.kill();
      await new Promise((done) =>
        server.exitCode === null ? server.once("exit", done) : done(),
      );
    }
  }
} finally {
  await browser.close();
  console.log(`Test logs and ephemeral identities: ${directory}`);
}
