import assert from "node:assert/strict";
import fs from "node:fs";
import { spawnSync } from "node:child_process";
import path from "node:path";
import {
  chromium,
  native,
  launch,
  page,
  media,
  until,
  delay,
  directory,
  peerArgs,
} from "./run-handoff.mjs";

// Browser-only and native-host -> browser-successor audio continuity. Every round
// adds a replacement, prepares it, kills the current host and checks both survivors.
const route = process.env.HANDOFF_ROUTE || "direct";
assert(
  ["direct", "relay"].includes(route),
  "HANDOFF_ROUTE must be direct or relay",
);
const losses = Number(process.env.HANDOFF_LOSSES || 3);
const dwell = Number(process.env.HANDOFF_DWELL_MS || 30000);
assert(Number.isInteger(losses) && losses >= 2 && losses <= 20);
assert(Number.isFinite(dwell) && dwell >= 1000);
const turn =
  route === "relay"
    ? {
        iceTransportPolicy: "relay",
        turnServers: [process.env.HANDOFF_TURN_URL],
        turnUsername: process.env.HANDOFF_TURN_USERNAME,
        turnCredential: process.env.HANDOFF_TURN_PASSWORD,
      }
    : {};
if (route === "relay")
  assert(
    turn.turnServers[0] && turn.turnUsername && turn.turnCredential,
    "Relay proof requires HANDOFF_TURN_URL/USERNAME/PASSWORD",
  );
const config = { audio: true, ...turn };
const snapshot = (p) =>
  p.evaluate(() => ({
    audio: window.handoffProbe.audioBlocks,
    last: window.handoffProbe.lastAudioAt,
    unique: window.handoffProbe.unique,
    migrations: window.handoffProbe.migrations,
    error: window.handoffProbe.error,
  }));
async function healthy(p, before = { audio: 0, unique: 0 }) {
  await media(p, before.unique);
  await until(
    async () => (await snapshot(p)).audio >= before.audio + 25,
    30000,
    "25 decoded non-silent remote audio blocks",
  );
  assert.equal((await snapshot(p)).error, "");
}
async function routes(p) {
  const value = await until(
    async () => {
      const pairs = await p.evaluate(() => window.handoffProbe.routes());
      return (
        pairs.length &&
        pairs.every(
          (pair) =>
            pair.state === "succeeded" &&
            pair.bytesReceived > 0 &&
            pair.bytesSent > 0,
        ) &&
        pairs
      );
    },
    30000,
    "selected ICE pairs carrying traffic",
  );
  if (route === "relay")
    assert(
      value.every((pair) => pair.local === "relay" && pair.remote === "relay"),
      JSON.stringify(value),
    );
  return value;
}
for (const [index, kind] of (
  process.env.HANDOFF_AUDIO_SCENARIOS || "browser,mixed"
)
  .split(",")
  .entries()) {
  assert(["browser", "mixed"].includes(kind));
  const base = Number(process.env.HANDOFF_PORT || 31000) + index * 20;
  const label = `${kind}-audio-${route}`;
  const result = {
    scenario: label,
    passed: false,
    losses,
    dwell,
    rounds: [],
    started: new Date().toISOString(),
  };
  const browsers = [],
    processes = [],
    participants = [];
  let host, sessionName;
  const start = async (create) => {
    // Separate browser processes make each host kill a genuine process loss.
    const server = await chromium.launchServer(launch);
    browsers.push(server);
    const browser = await chromium.connect(server.wsEndpoint());
    const p = await page(browser, `${label}-${browsers.length}`);
    await p.evaluate(
      ({ create, sessionName, base, config }) =>
        window.handoffProbe.start(
          create,
          sessionName || "",
          `ws://127.0.0.1:${base + 1}`,
          config,
        ),
      { create, sessionName, base, config },
    );
    await until(() => p.evaluate(() => window.handoffProbe.participant));
    participants.push(p);
    return {
      p,
      pid: server.process().pid,
      kill: () => {
        const processToKill = server.process();
        if (process.platform === "win32") {
          const killed = spawnSync("taskkill", [
            "/PID",
            String(processToKill.pid),
            "/T",
            "/F",
          ]);
          // Children can exit between taskkill's enumeration and termination.
          // Verify the root is gone instead of treating that race as failure.
          try {
            process.kill(processToKill.pid, 0);
            assert.fail(
              killed.stderr?.toString() || "Host process survived taskkill",
            );
          } catch (error) {
            if (error.code !== "ESRCH") throw error;
          }
        } else assert(processToKill.kill("SIGKILL"));
      },
    };
  };
  try {
    const acds = native(
      [
        "discovery-service",
        "--port",
        String(base),
        "--websocket-port",
        String(base + 1),
        "--database",
        path.join(directory, `${label}-${Date.now()}.db`),
        "--status-screen=false",
      ],
      `${label}-acds`,
    );
    processes.push(acds);
    await until(() => /listening/i.test(acds.read()), 15000, "ACDS readiness");
    if (kind === "browser") {
      host = await start(true);
      sessionName = await host.p.evaluate(() => window.handoffProbe.name);
    } else {
      const args = peerArgs("", base, base + 2);
      args.splice(args.indexOf("--audio=false"), 1, "--test-audio");
      if (route === "relay") {
        args.splice(
          args.indexOf("--turn-servers") + 1,
          1,
          process.env.HANDOFF_TURN_URL,
        );
        args.push(
          "--webrtc-relay-only",
          "--turn-username",
          process.env.HANDOFF_TURN_USERNAME,
          "--turn-credential",
          process.env.HANDOFF_TURN_PASSWORD,
        );
      }
      const child = native(args, `${label}-native-host`);
      processes.push(child);
      host = { kill: () => child.child.kill("SIGKILL") };
      sessionName = await until(
        () =>
          child
            .read()
            .match(/Share this string to connect: ([a-z]+-[a-z]+-[a-z]+)/)?.[1],
      );
    }
    let survivors = [await start(false)];
    if (kind === "mixed") {
      // With only the native host and one browser, decoded remote PCM must
      // come from the native deterministic source, not another browser.
      await healthy(survivors[0].p);
      result.nativeSource = await snapshot(survivors[0].p);
    }
    survivors.push(await start(false));
    for (let round = 0; round < losses; round++) {
      await Promise.all(survivors.map(({ p }) => healthy(p)));
      await delay(dwell);
      const beforeRoutes = await Promise.all(
        survivors.map(({ p }) => routes(p)),
      );
      const successor = await until(
        async () => {
          for (const candidate of survivors) {
            const prepared = await candidate.p.evaluate(
              () =>
                window.handoffProbe.status ===
                `Successor prepared: ${window.handoffProbe.participant}`,
            );
            if (prepared) return candidate;
          }
        },
        30000,
        "prepared successor among survivors",
      );
      const before = await Promise.all(survivors.map(({ p }) => snapshot(p)));
      const killedAt = Date.now();
      result.killing = { pid: host.pid, before, at: killedAt };
      host.kill();
      const measurements = await Promise.all(
        survivors.map(async ({ p }, i) => {
          await until(
            async () => (await snapshot(p)).migrations > before[i].migrations,
            30000,
            "host commit",
          );
          // Snapshot AFTER commit: queued old-host packets cannot satisfy recovery.
          const committed = await snapshot(p);
          await healthy(p, committed);
          const after = await snapshot(p);
          const timing = await p.evaluate(
            ({ last, blocks }) => {
              const times = window.handoffProbe.audioTimes.slice(blocks);
              return {
                interruptionMs: times[0] - last,
                maxGapMs: Math.max(
                  ...times.map((t, n) => t - (n ? times[n - 1] : last)),
                ),
              };
            },
            { last: before[i].last, blocks: committed.audio },
          );
          const selectedPairs = await routes(p);
          await p.screenshot({
            path: path.join(directory, `${label}-round-${round}-${i}.png`),
          });
          return {
            before: before[i],
            committed,
            after,
            ...timing,
            recoveryObservedMs: Date.now() - killedAt,
            selectedPairs,
          };
        }),
      );
      result.rounds.push({ round, beforeRoutes, measurements });
      host = successor;
      survivors = survivors.filter((p) => p !== successor);
      if (round + 1 < losses) survivors.push(await start(false));
    }
    result.passed = true;
  } catch (error) {
    result.error = error.stack;
    result.participants = await Promise.all(
      participants.map(async (p) => ({
        ...(await snapshot(p).catch(() => ({}))),
        status: await p
          .evaluate(() => window.handoffProbe.status)
          .catch(String),
      })),
    );
    throw error;
  } finally {
    result.completed = new Date().toISOString();
    result.nativeProcesses = processes.map((p) => ({
      log: p.log,
      exitCode: p.child.exitCode,
      signal: p.child.signalCode,
    }));
    fs.writeFileSync(
      path.join(directory, `${label}-result.json`),
      JSON.stringify(result, null, 2),
    );
    for (const p of processes)
      if (p.child.exitCode === null) p.child.kill("SIGKILL");
    for (const server of browsers)
      await Promise.race([server.close().catch(() => {}), delay(5000)]);
    console.log(JSON.stringify(result));
  }
}
