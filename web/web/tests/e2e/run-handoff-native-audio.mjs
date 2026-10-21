import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { native, peerArgs, until, delay, directory } from "./run-handoff.mjs";

const losses = Number(process.env.HANDOFF_NATIVE_LOSSES || 3);
const dwell = Number(process.env.HANDOFF_DWELL_MS || 30000);
const base = Number(process.env.HANDOFF_PORT || 35000);
assert(Number.isInteger(losses) && losses >= 2 && losses <= 3);
assert(Number.isFinite(dwell) && dwell >= 1000);
const audio = (text) =>
  [
    ...text.matchAll(
      /Discovery decoded remote audio: blocks=(\d+) samples=(\d+) at_ns=(\d+) max_gap_ns=(\d+)/g,
    ),
  ].map((m) => ({
    blocks: Number(m[1]),
    samples: Number(m[2]),
    atNs: Number(m[3]),
    maxGapNs: Number(m[4]),
  }));
const processes = [];
const result = {
  scenario: "native-audio-direct",
  passed: false,
  losses,
  dwell,
  rounds: [],
  started: new Date().toISOString(),
};
const peer = (name, index) => {
  const args = peerArgs(name, base, base + 2 + index);
  args.splice(args.indexOf("--audio=false"), 1, "--test-audio");
  args.push("--speakers-volume", "0");
  args.splice(
    args.indexOf("--fps") + 1,
    1,
    String(process.env.HANDOFF_NATIVE_FPS || 20),
  );
  const p = native(args, `native-audio-${index}`);
  processes.push(p);
  return p;
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
      path.join(directory, `native-audio-${Date.now()}.db`),
      "--status-screen=false",
    ],
    "native-audio-acds",
  );
  processes.push(acds);
  await until(() => /listening/i.test(acds.read()), 15000, "ACDS readiness");
  let host = peer("", 0);
  const name = await until(
    () =>
      host
        .read()
        .match(/Share this string to connect: ([a-z]+-[a-z]+-[a-z]+)/)?.[1],
  );
  let live = [host];
  for (let i = 1; i < 3; i++) {
    const p = peer(name, i);
    p.identity = await until(
      () => p.read().match(/Handoff identity=([0-9a-f]{8})/)?.[1],
    );
    live.push(p);
    await until(
      () =>
        audio(p.read()).some((a) => a.blocks >= 25) &&
        /Discovery decoded video: frames=25/.test(p.read()),
      30000,
      "native decoded remote audio and rendered media",
    );
  }
  let nextPeer = 3;
  for (let round = 0; round < losses; round++) {
    await delay(dwell);
    const successor = await until(
      () => {
        for (const p of live.filter((p) => p !== host)) {
          const prepared = [
            ...p.read().matchAll(/Prepared successor ([0-9a-f]{8})/g),
          ].at(-1)?.[1];
          if (prepared === p.identity) return p;
        }
      },
      30000,
      "prepared native successor",
    );
    const before = new Map(
      live.map((p) => [
        p,
        { offset: p.read().length, audio: audio(p.read()).at(-1) },
      ]),
    );
    const killedAt = Date.now();
    host.child.kill("SIGKILL");
    live = live.filter((p) => p !== host);
    await Promise.all(
      live.map((p) =>
        until(
          () =>
            /Committed successor/.test(p.read().slice(before.get(p).offset)),
          30000,
          "native successor commit",
        ),
      ),
    );
    await until(
      () =>
        /Hosting session/.test(
          successor.read().slice(before.get(successor).offset),
        ),
      30000,
      "native successor host media loop",
    );
    const observations = await Promise.all(
      live
        .filter((p) => p !== successor)
        .map(async (p) => {
          await until(
            () => {
              const resumed = p.read().slice(before.get(p).offset);
              const active = resumed.lastIndexOf("Discovery state: ACTIVE");
              const fresh = active < 0 ? "" : resumed.slice(active);
              return (
                /Discovery decoded video: frames=25/.test(fresh) &&
                audio(fresh).some((a) => a.blocks >= 25)
              );
            },
            30000,
            "fresh native remote decoded PCM and rendered frames after reconnect",
          );
          const resumed = p.read().slice(before.get(p).offset);
          const fresh = resumed.slice(
            resumed.lastIndexOf("Discovery state: ACTIVE"),
          );
          const first = audio(fresh)[0];
          assert(
            first.atNs > before.get(p).audio.atNs,
            "Recovery must contain fresh monotonic audio observations",
          );
          return {
            participant: p.identity,
            before: before.get(p).audio,
            firstAfter: first,
            interruptionUpperBoundMs:
              (first.atNs - before.get(p).audio.atNs) / 1e6,
            recoveryObservedMs: Date.now() - killedAt,
            after: audio(fresh).at(-1),
          };
        }),
    );
    result.rounds.push({ round, successor: successor.identity, observations });
    host = successor;
    if (round + 1 < losses) {
      const replacement = peer(name, nextPeer++);
      replacement.identity = await until(
        () => replacement.read().match(/Handoff identity=([0-9a-f]{8})/)?.[1],
      );
      live.push(replacement);
      await until(
        () =>
          audio(replacement.read()).some((a) => a.blocks >= 25) &&
          /Discovery decoded video: frames=25/.test(replacement.read()),
        30000,
        "replacement native media",
      );
    }
  }
  result.passed = true;
} catch (error) {
  result.error = error.stack;
  throw error;
} finally {
  result.completed = new Date().toISOString();
  result.nativeProcesses = processes.map((p) => ({
    log: p.log,
    exitCode: p.child.exitCode,
    signal: p.child.signalCode,
  }));
  fs.writeFileSync(
    path.join(directory, "native-audio-direct-result.json"),
    JSON.stringify(result, null, 2),
  );
  for (const p of processes)
    if (p.child.exitCode === null) p.child.kill("SIGKILL");
  console.log(JSON.stringify(result));
}
