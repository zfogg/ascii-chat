import { expect, test } from "./browser-fixture";
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";

const version = createHash("sha256")
  .update(readFileSync("src/wasm/dist/manifest.json"))
  .digest("hex")
  .slice(0, 32);

test("published WASM manifest declares module capabilities", async ({
  page,
}) => {
  await page.goto("/client");
  const manifest = await page.evaluate(async () =>
    (await fetch("/wasm/manifest.json")).json(),
  );
  expect(manifest.capabilities.client.transports).toEqual([
    "webrtc",
    "webtransport",
    "websocket",
  ]);
  expect(manifest.capabilities.mirror.features.renderer).toBe(true);
  expect(manifest.capabilities.client.codecs).toContain("hevc");
});

test("client protocol metadata comes from the native session binding", async ({ page }) => {
  await page.goto("/client");
  const info = await page.evaluate(async () => {
    const { initClientWasm, getClientWasmModule } =
      await import(/* @vite-ignore */ "/src/wasm/client.ts");
    await initClientWasm();
    const session = new (getClientWasmModule()!).ClientSessionBinding({});
    const value = session.protocolInfo();
    session.close();
    session.delete();
    return value;
  });
  expect(info.protocolVersion).toBe(1);
  expect(info.connectionStates).toEqual([
    "disconnected",
    "connecting",
    "handshake",
    "connected",
    "error",
  ]);
  expect(info.cryptoModes.none).toBe(0);
  expect(info.cryptoModes.encrypt).toBeGreaterThan(0);
});

test("binding failures record actionable C errors", async ({ page }) => {
  await page.goto("/client");
  const errors = await page.evaluate(async () => {
    const moduleUrl = "/src/wasm/client.ts";
    const { initClientWasm, getClientWasmModule, processClientEvent } =
      await import(/* @vite-ignore */ moduleUrl);
    await initClientWasm();
    const m =
      getClientWasmModule() as unknown as import("../../../packages/shared/src/wasm/generated/client-bindings").MainModule;
    const frame = new m.MirrorFrameBinding();
    const processor = new m.AudioProcessorBinding(48000, true, 16);
    const renderer = new m.AsciiRendererBinding(2, 2, 12, false, 0);
    const session = new m.ClientSessionBinding({});
    const structuredError = session.process({ type: "unknown-event" });
    if (
      structuredError.code === 0 ||
      typeof structuredError.message !== "string" ||
      typeof structuredError.retryable !== "boolean"
    ) {
      throw new Error("Client session did not return a structured error");
    }
    const snapshot = (run: () => unknown) => {
      m.ErrorBinding.dismiss();
      try {
        run();
      } catch {
        // The public TypeScript helper may throw after recording the C error.
      }
      const count = m.ErrorBinding.snapshot();
      return count ? m.ErrorBinding.frame(0) : null;
    };
    try {
      return [
        snapshot(() => frame.convert(new Uint8Array(1), 1, 1, false)),
        snapshot(() => frame.transform(new Uint8Array(1), 1, 1)),
        snapshot(() =>
          processor.process(new Float32Array(4), new Float32Array(1)),
        ),
        snapshot(() => renderer.render("", 0)),
        snapshot(() =>
          new m.FaceFollowBinding().detect(new Uint8ClampedArray(1), 1, 1, 0),
        ),
        snapshot(() =>
          session.process({
            type: "receive-handshake-packet",
            payload: new Uint8Array(1),
          }),
        ),
        snapshot(() => {
          const result = processClientEvent({
            type: "parse",
            packet: new Uint8Array(),
          });
          if (result.code)
            throw new Error(`Packet parse failed (${result.code})`);
        }),
        snapshot(() => m.KeyParserBinding.parse(99, false, new Uint8Array())),
        snapshot(() => {
          const result = processClientEvent({
            type: "handoff-encode",
            kind: 1,
            session: new Uint8Array(1),
            participant: new Uint8Array(16),
            generation: 0,
            sequence: 0,
            capabilities: 0,
            natTier: 0,
            port: 0,
          });
          if (result.code)
            throw new Error(`Handoff encode failed (${result.code})`);
        }),
      ].filter(Boolean);
    } finally {
      m.ErrorBinding.dismiss();
      renderer.delete();
      session.delete();
      processor.delete();
      frame.delete();
    }
  });
  expect(errors).toHaveLength(8);
  for (const error of errors) {
    expect([85, 86]).toContain(error?.code);
    expect(error?.message).toBeTruthy();
  }
});

test("browser session close makes later processing safe and structured", async ({ page }) => {
  await page.goto("/client");
  const result = await page.evaluate(async () => {
    const { initClientWasm, getClientWasmModule } =
      await import(/* @vite-ignore */ "/src/wasm/client.ts");
    await initClientWasm();
    const module = getClientWasmModule()!;
    const session = new module.BrowserSessionBinding(false, 0, new Uint8Array(), new Uint8Array(), new Uint8Array());
    session.close();
    return session.process({ type: "snapshot" });
  });
  expect(result.code).not.toBe(0);
  expect(result.message).toContain("not initialized");
  expect(result.retryable).toBe(false);
  expect(result.writable).toBe(false);
});

test("session UI binding close is idempotent and clears ownership", async ({ page }) => {
  await page.goto("/client");
  const result = await page.evaluate(async () => {
    const { initClientWasm, getClientWasmModule } =
      await import(/* @vite-ignore */ "/src/wasm/client.ts");
    await initClientWasm();
    const ui = new (getClientWasmModule()!).SessionUiBinding(false);
    const before = { valid: ui.valid(), error: ui.error() };
    ui.close();
    ui.close();
    const after = {
      valid: ui.valid(),
      error: ui.error(),
      key: ui.key(13),
      media: ui.media("late input"),
      frame: ui.tick(0),
    };
    ui.delete();
    return { before, after };
  });
  expect(result.before).toEqual({ valid: true, error: 0 });
  expect(result.after.valid).toBe(false);
  expect(result.after.error).not.toBe(0);
  expect(result.after.key).not.toBe(0);
  expect(result.after.media).not.toBe(0);
  expect(result.after.frame).toBeUndefined();
});

test("mirror frame binding owns and safely releases render state", async ({ page }) => {
  await page.goto("/client");
  const result = await page.evaluate(async () => {
    const { initClientWasm, getClientWasmModule } =
      await import(/* @vite-ignore */ "/src/wasm/client.ts");
    await initClientWasm();
    const frame = new (getClientWasmModule()!).MirrorFrameBinding();
    const before = { valid: frame.valid(), error: frame.error() };
    frame.close();
    frame.close();
    const after = {
      valid: frame.valid(),
      error: frame.error(),
      audio: frame.renderAudio(40, 12, 0),
    };
    frame.delete();
    return { before, after };
  });
  expect(result.before).toEqual({ valid: true, error: 0 });
  expect(result.after.valid).toBe(false);
  expect(result.after.error).not.toBe(0);
  expect(result.after.audio).toBeUndefined();
});

test("key parsing preserves unrelated errors and returns only its own frames", async ({
  page,
}) => {
  await page.goto("/client");
  const result = await page.evaluate(async () => {
    const moduleUrl = "/src/wasm/client.ts";
    const { initClientWasm, getClientWasmModule } = await import(
      /* @vite-ignore */ moduleUrl
    );
    await initClientWasm();
    const module = getClientWasmModule();
    module.ErrorBinding.dismiss();
    const session = new module.ClientSessionBinding({});
    session.process({
      type: "receive-handshake-packet",
      payload: new Uint8Array(),
    });
    const beforeCount = module.ErrorBinding.snapshot();
    const before = module.ErrorBinding.frame(0);
    const parsed = module.KeyParserBinding.parse(3, true, new Uint8Array());
    const afterCount = module.ErrorBinding.snapshot();
    const pending = Array.from({ length: afterCount }, (_, i) =>
      module.ErrorBinding.frame(i),
    );
    module.ErrorBinding.dismiss();
    session.delete();
    return { beforeCount, before, parsed, pending };
  });
  expect(result.beforeCount).toBeGreaterThan(0);
  expect(result.parsed.code).not.toBe(0);
  expect(result.parsed.frames.length).toBeGreaterThan(0);
  expect(result.parsed.frames[0]?.message).toContain(
    "binary OpenPGP public key",
  );
  expect(
    result.parsed.frames.some(
      (frame: { message: string }) => frame.message === result.before.message,
    ),
  ).toBe(false);
  expect(
    result.pending.some((frame) => frame.message === result.before.message),
  ).toBe(true);
});

test("face tracking and client public keys use the native bindings", async ({
  page,
}) => {
  await page.goto("/client");
  const result = await page.evaluate(async () => {
    const moduleUrl = "/src/wasm/client.ts";
    const { initClientWasm, getClientWasmModule, generateKeypair } =
      await import(/* @vite-ignore */ moduleUrl);
    await initClientWasm();
    const module = getClientWasmModule();
    const face = new module.FaceFollowBinding();
    face.reset();
    const faceStatus = face.detect(
      new Uint8ClampedArray(16 * 16 * 4),
      16,
      16,
      1,
    );
    const faceValues = Array.from(face.tick(1, true));
    const key = await generateKeypair();
    module.KeyParserBinding.parse(99, false, new Uint8Array());
    module.KeyParserBinding.parse(3, true, new Uint8Array());
    const parseError = module.KeyParserBinding.error();
    return { faceStatus, face: faceValues, key, parseError };
  });
  expect(result.faceStatus).toBe(0);
  expect(result.face).toHaveLength(11);
  expect(result.face[9]).toBe(0);
  expect(result.key).toMatch(/^[0-9a-f]{64}$/);
  expect(result.parseError).toContain("Invalid binary OpenPGP public key");
});

test("client sessions own independent state and support reinitialization", async ({
  page,
}) => {
  await page.goto("/client");
  const result = await page.evaluate(async () => {
    const url = "/src/wasm/dist/client.js";
    const factory = (await import(/* @vite-ignore */ url)).default;
    const module = await factory({
      locateFile: (name: string) => "/src/wasm/dist/" + name,
      getRandomValue: () => crypto.getRandomValues(new Uint32Array(1))[0],
    });
    const first = new module.ClientSessionBinding({ width: 48, height: 24 });
    const overlap = new module.ClientSessionBinding({ width: 48, height: 24 });
    const result = {
      first: first.valid(),
    overlap: overlap.valid(),
      close: typeof first.close,
      width: module.OptionsBinding.read()?.values.width,
      key: -1,
      secondKey: -1,
      secondAfterClose: -1,
      reinitialized: false,
    };
    result.key = first.process({ type: "generate-keypair" }).code;
    result.secondKey = overlap.process({ type: "generate-keypair" }).code;
    first.close();
    first.delete();
    result.secondAfterClose = overlap.process({ type: "snapshot" }).code;
    overlap.close();
    overlap.delete();
    const next = new module.ClientSessionBinding({});
    result.reinitialized = next.valid();
    next.delete();
    return result;
  });
  expect(result).toEqual({
    first: true,
    overlap: true,
    width: 48,
    key: 0,
    secondKey: 0,
    secondAfterClose: 0,
    reinitialized: true,
    close: "function",
  });
});

test("browser ABI exposes owned session names without legacy peer bindings", async ({
  page,
}) => {
  await page.goto("/client");
  const names = await page.evaluate(async () => {
    const url = "/src/wasm/dist/client.js";
    const factory = (await import(/* @vite-ignore */ url)).default;
    const module = await factory({
      locateFile: (name: string) => "/src/wasm/dist/" + name,
      getRandomValue: () => crypto.getRandomValues(new Uint32Array(1))[0],
    });
    return {
      client: typeof module.ClientSessionBinding,
      browser: typeof module.BrowserSessionBinding,
      oldPeer: "PeerCryptoBinding" in module,
    };
  });
  expect(names).toEqual({
    client: "function",
    browser: "function",
    oldPeer: false,
  });
});

test("client session exposes lifecycle snapshots and transport events", async ({
  page,
}) => {
  await page.goto("/client");
  const result = await page.evaluate(async () => {
    const factory = (await import("/src/wasm/dist/client.js")).default;
    const module = await factory({
      locateFile: (name: string) => "/src/wasm/dist/" + name,
      getRandomValue: () => crypto.getRandomValues(new Uint32Array(1))[0],
    });
    const session = new module.ClientSessionBinding({});
    const initial = session.process({ type: "snapshot" });
    const opening = session.process({ type: "transport-open" });
    const connected = session.process({ type: "transport-connected" });
    const ready = session.process({ type: "snapshot" });
    const drained = session.drainOutputs();
    const batch = session.processBatch([
      { type: "transport-open" },
      { type: "transport-connected" },
      { type: "snapshot" },
    ]);
    const oversized = session.processBatch(
      Array.from({ length: 257 }, () => ({ type: "snapshot" })),
    );
    session.close();
    const closedBatch = session.processBatch([]);
    session.process({ type: "transport-closed" });
    session.delete();
    return {
      initial: initial.state,
      opening: opening.state,
      connected: connected.state,
      ready: ready.ready,
      writable: ready.writable,
      drained: {
        code: drained.code,
        epoch: drained.epoch,
        outputs: drained.outputs.length,
      },
      batch: batch.map((entry: { state: number; ready?: boolean }) => ({
        state: entry.state,
        ready: entry.ready ?? false,
      })),
      closedBatchCode: closedBatch[0]?.code,
      oversizedBatchCode: oversized[0]?.code,
    };
  });
  expect(result).toEqual({
    initial: 0,
    opening: 1,
    connected: 3,
    ready: true,
    writable: true,
    drained: { code: expect.any(Number), epoch: -1, outputs: 0 },
    batch: [
      { state: 1, ready: false },
      { state: 3, ready: false },
      { state: 3, ready: true },
    ],
    closedBatchCode: expect.any(Number),
    oversizedBatchCode: expect.any(Number),
  });
});

test("mirror initialization applies and validates an options object", async ({
  page,
}) => {
  await page.goto("/client");
  const result = await page.evaluate(async () => {
    const base = "/src/wasm/dist/";
    const factory = (await import(/* @vite-ignore */ `${base}mirror.js`))
      .default;
    const m = await factory({
      locateFile: (name: string) => base + name,
      getRandomValue: () => crypto.getRandomValues(new Uint32Array(1))[0],
    });
    const rejectedSession = new m.MirrorSessionBinding({ width: 48, audio_source: 99 });
    const rejected = rejectedSession.error();
    rejectedSession.delete();
    const acceptedSession = new m.MirrorSessionBinding({
      width: 48,
      audio_source: 3,
      waveform: true,
    });
    const accepted = acceptedSession.valid() ? 0 : acceptedSession.error();
    const values = m.OptionsBinding.read()?.values;
    acceptedSession.close();
    acceptedSession.delete();
    return {
      rejected,
      accepted,
      width: values?.width,
      waveform: values?.waveform,
    };
  });
  expect(result.rejected).not.toBe(0);
  expect(result.accepted).toBe(0);
  expect(result.width).toBe(48);
  expect(result.waveform).toBe(true);
});

test("Embind owns audio buffers and encrypted peer sessions", async ({
  page,
}) => {
  await page.goto("/client");
  const result = await page.evaluate(async () => {
    const moduleUrl = "/src/wasm/client.ts";
    const {
      initClientWasm,
      getClientWasmModule,
      BrowserSession,
      processClientEvent,
      PacketType,
    } = await import(/* @vite-ignore */ moduleUrl);
    await initClientWasm();
    const module = getClientWasmModule();
    const serializePacket = (packetType: number, payload: Uint8Array) => {
      const result = processClientEvent({
        type: "serialize",
        packetType,
        payload,
        clientId: 0,
      });
      if (result.code || !result.bytes)
        throw new Error(`Serialization failed (${result.code})`);
      return result.bytes;
    };
    const dsp = new module.AudioProcessorBinding(48000, true, 1920);
    const input = new Float32Array(1920).fill(0.25);
    const output = new Float32Array(input.length);
    const audio = {
      valid: dsp.valid(),
      status: dsp.process(input, output),
      sample: output[0],
      bad: dsp.process(input, new Float32Array(1)),
      batch: dsp.processBatch(
        [input, input],
        [new Float32Array(input.length), new Float32Array(input.length)],
      ),
    };
    dsp.delete();
    const mono = new module.AudioProcessorBinding(48000, true, 960, 1);
    const monoInput = new Float32Array(13).fill(0.125);
    const monoOutput = new Float32Array(13);
    const monoStatus = mono.process(monoInput, monoOutput);
    const monoSample = monoOutput[0];
    mono.delete();
    const codec = new module.OpusCodecBinding(48000, 2, 64000, 4000);
    const competingCodec = new module.OpusCodecBinding(48000, 2, 64000, 4000);
    const competingAccepted = competingCodec.valid();
    competingCodec.delete();
    const pcm = Int16Array.from({ length: 1920 }, (_, i) =>
      Math.round(5000 * Math.sin(i / 13)),
    );
    const encoded = codec.encode(pcm);
    const decoded = encoded ? codec.decode(encoded, 960) : null;
    const snapshot = {
      encoded: Array.from(encoded ?? []),
      decoded: Array.from(decoded ?? []),
    };
    const reused = codec.encode(new Int16Array(1920));
    const retainedAfterReuse = {
      encoded: Array.from(encoded ?? []),
      decoded: Array.from(decoded ?? []),
    };
    const opus = {
      valid: codec.valid(),
      competingAccepted,
      bytes: encoded?.length ?? 0,
      samples: decoded?.length ?? 0,
      invalid: codec.encode(new Int16Array(12000)),
    };
    codec.delete();
    const retainedAfterDelete = {
      encoded: Array.from(encoded ?? []),
      decoded: Array.from(decoded ?? []),
    };
    processClientEvent({
      type: "configure-crypto",
      password: new Uint8Array(),
      identity: new Uint8Array(3),
      expected: new Uint8Array(),
    });
    const invalidPeer = new module.BrowserSessionBinding(
      false,
      1,
      new Uint8Array(),
      new Uint8Array(3),
      new Uint8Array(),
    );
    const invalidPeerError = invalidPeer.error();
    invalidPeer.delete();
    module.ErrorBinding.dismiss();
    const invalidConfig = processClientEvent({
      type: "configure-crypto",
      password: new Uint8Array(),
      identity: new Uint8Array(3),
      expected: new Uint8Array(),
    });
    module.ErrorBinding.snapshot();
    const invalidConfigError = module.ErrorBinding.frame(0).message;
    module.ErrorBinding.dismiss();

    async function exercise(quic: boolean) {
      const toClient: Uint8Array[] = [],
        toServer: Uint8Array[] = [];
      const client = await BrowserSession.create(
        { password: "password-for-interop-test" },
        true,
        false,
        (p: Uint8Array) => toServer.push(p),
      );
      const server = await BrowserSession.create(
        { password: "password-for-interop-test" },
        true,
        true,
        (p: Uint8Array) => toClient.push(p),
      );
      try {
        client.flush();
        server.flush();
        for (
          let i = 0;
          i < 100 &&
          (!client.ready ||
            !server.ready ||
            toClient.length ||
            toServer.length);
          i++
        ) {
          while (toServer.length) server.receive(toServer.shift()!);
          while (toClient.length) client.receive(toClient.shift()!);
        }
        if (!client.ready || !server.ready)
          throw new Error(
            `Peer handshake did not finish: client=${client.ready} server=${server.ready} c2s=${toServer.length} s2c=${toClient.length}`,
          );
        const original = serializePacket(PacketType.PING, new Uint8Array(8));
        let decoded: Uint8Array | null = null;
        let sequence = "";
        if (quic) {
          client.initQuic();
          server.initQuic();
          const sealed = client.quicPacket("seal", 0, 0, original);
          if (
            Array.from(sealed.packet).join(",") ===
            Array.from(original).join(",")
          )
            throw new Error("QUIC packet was not protected");
          const opened = server.quicPacket("open", 0, 0, sealed.packet);
          decoded = opened.packet;
          sequence = opened.sequence.toString();
          const altered = client
            .quicPacket("seal", 0, 0, original)
            .packet.slice();
          altered[altered.length - 1] = altered[altered.length - 1]! ^ 1;
          let rejected = false;
          try {
            server.quicPacket("open", 0, 0, altered);
          } catch {
            rejected = true;
          }
          if (!rejected) throw new Error("Altered QUIC record was accepted");
        } else {
          client.sendPacket(original);
          if (
            toServer.length === 0 ||
            Array.from(toServer[0]!).join(",") ===
              Array.from(original).join(",")
          )
            throw new Error("Classic packet was not protected");
          while (toServer.length)
            decoded = server.receive(toServer.shift()!) ?? decoded;
          client.sendPacket(original);
          const altered = toServer.shift()!.slice();
          altered[altered.length - 1] = altered[altered.length - 1]! ^ 1;
          let rejected = false;
          try {
            server.receive(altered);
          } catch {
            rejected = true;
          }
          if (!rejected) throw new Error("Altered classic record was accepted");
        }
        return {
          same:
            !!decoded &&
            Array.from(decoded).join(",") === Array.from(original).join(","),
          sequence,
        };
      } finally {
        client.close();
        server.close();
      }
    }
    return {
      audio,
      monoStatus,
      monoSample,
      opus,
      snapshot,
      retainedAfterReuse,
      retainedAfterDelete,
      reusedBytes: reused?.length ?? 0,
      invalidPeerError,
      invalidConfig,
      invalidConfigError,
      classic: await exercise(false),
      quic: await exercise(true),
    };
  });
  expect(result.audio).toEqual({
    valid: true,
    status: 0,
    sample: 0.25,
    bad: 86,
    batch: 0,
  });
  expect(result.monoStatus).toBe(0);
  expect(result.monoSample).toBe(0.125);
  expect(result.opus.valid).toBe(true);
  expect(result.opus.competingAccepted).toBe(true);
  expect(result.opus.bytes).toBeGreaterThan(0);
  expect(result.opus.samples).toBe(1920);
  expect(result.reusedBytes).toBeGreaterThan(0);
  expect(result.retainedAfterReuse).toEqual(result.snapshot);
  expect(result.retainedAfterDelete).toEqual(result.snapshot);
  expect(result.invalidPeerError).toContain(
    "peer identity or verification key length",
  );
  expect(result.invalidConfig).not.toBe(0);
  expect(result.invalidConfigError).toContain("key length");
  expect(result.opus.invalid).toBeNull();
  expect(result.classic.same).toBe(true);
  expect(result.quic.same).toBe(true);
  expect(result.quic.sequence).toMatch(/^\d+$/);
});

test("key parser worker returns owned public key bytes and reports invalid input", async ({
  page,
}) => {
  await page.goto("/client");
  const privateKey = readFileSync(
    "tests/fixtures/crypto/client-identity",
    "utf8",
  );
  const publicKey = readFileSync(
    "tests/fixtures/crypto/client-identity.pub",
    "utf8",
  );
  const result = await page.evaluate(
    async ({ privateKey, publicKey }) => {
      const moduleUrl = "/src/wasm/client.ts";
      const { validateSshPublicKey, validateSshPrivateKey } = await import(
        /* @vite-ignore */ moduleUrl
      );
      const encoded =
        "AAAAC3NzaC1lZDI1NTE5AAAAIGPkW7kWr6FXmS8j1YJv4VoxXu+XYC+oPOC5AXsB/3kT";
      const parsed = await validateSshPublicKey(
        `ssh-ed25519 ${encoded} test@example.com`,
      );
      const expected = Uint8Array.from(atob(encoded), (character) =>
        character.charCodeAt(0),
      ).slice(-32);
      const secret = await validateSshPrivateKey(privateKey);
      const matchingPublic = await validateSshPublicKey(publicKey);
      const privateMatches =
        Array.from(secret.slice(32)).join(",") ===
        Array.from(matchingPublic).join(",");
      secret.fill(0);
      const body = Uint8Array.from([
        4,
        0,
        0,
        0,
        0,
        22,
        9,
        0x2b,
        6,
        1,
        4,
        1,
        0xda,
        0x47,
        0x0f,
        1,
        1,
        7,
        0x40,
        ...expected,
      ]);
      const packet = Uint8Array.from([0xc6, body.length, ...body]);
      const pgp = `ascii-chat:openpgp-binary-base64:${btoa(String.fromCharCode(...packet))}`;
      const parsedPgp = await validateSshPublicKey(pgp);
      let rejected = false;
      try {
        await validateSshPublicKey("invalid key");
      } catch {
        rejected = true;
      }
      return {
        parsed: Array.from(parsed),
        expected: Array.from(expected),
        parsedPgp: Array.from(parsedPgp),
        privateMatches,
        rejected,
      };
    },
    { privateKey, publicKey },
  );
  expect(result.parsed).toEqual(result.expected);
  expect(result.parsedPgp).toEqual(result.expected);
  expect(result.privateMatches).toBe(true);
  expect(result.rejected).toBe(true);
});

test("renderer copies stay valid across WASM memory growth", async ({
  page,
}) => {
  await page.goto("/tests/e2e/raster-worker.html?mode=mirror");
  const result = await page.evaluate(async (artifactVersion) => {
    const base = `/wasm/${artifactVersion}/`;
    const factory = (await import(/* @vite-ignore */ `${base}mirror.js`))
      .default;
    const m = await factory({
      locateFile: (file: string) => `${base}${file}`,
      pthreadPoolSize: 0,
      getRandomValue: () => crypto.getRandomValues(new Uint32Array(1))[0],
    });
    const mirror = new m.MirrorSessionBinding({});
    if (!mirror.valid()) throw new Error(`mirror initialization failed: ${mirror.error()}`);
    const frame = new m.MirrorFrameBinding();
    const bad = frame.convert(new Uint8Array(4), 2, 2, true);
    const samples = Float32Array.from({ length: 16384 }, (_, i) =>
      Math.sin(i / 19),
    );
    frame.submitAudio(samples, 0);
    frame.submitAudio(samples.subarray(0, 8192), 1);
    frame.submitAudio(samples.subarray(8192), 1);
    const largeAudio = frame.renderAudio(40, 12, 0);
    const chunkedAudio = frame.renderAudio(40, 12, 1);
    const renderer = new m.AsciiRendererBinding(20, 10, 16, false, 1);
    if (!renderer.valid())
      throw new Error(`renderer creation failed: ${renderer.error()}`);
    try {
      const pixels = renderer.render("memory growth test", 0);
      if (!pixels) throw new Error("pixel render failed");
      const snapshot = Array.from(pixels);
      const before = m.HEAPU8.byteLength;
      const largeFrame = new Uint8Array(before);
      if (frame.transform(largeFrame, before / 4, 1))
        throw new Error("could not force memory growth");
      const after = m.HEAPU8.byteLength;
      const next = renderer.render("memory growth test", 0);
      if (!next) throw new Error("pixel render after growth failed");
      return {
        before,
        after,
        rejected: bad === undefined,
        largeAudio,
        chunkedAudio,
        retained: snapshot.every((byte, i) => pixels[i] === byte),
        same: snapshot.every((byte, i) => next[i] === byte),
      };
    } finally {
      renderer.delete();
      frame.delete();
      mirror.close();
      mirror.delete();
    }
  }, version);
  expect(result.after).toBeGreaterThan(result.before);
  expect(result.rejected).toBe(true);
  expect(result.largeAudio).toBeTruthy();
  expect(result.largeAudio).toBe(result.chunkedAudio);
  expect(result.retained).toBe(true);
  expect(result.same).toBe(true);
});

test("client runtime initializes after its versioned module loads", async ({
  page,
}) => {
  await page.goto("/client");
  const diagnostics = await page.evaluate(async () => {
    const moduleUrl = "/src/wasm/client.ts";
    const { initClientWasm, cleanupClientWasm, getClientWasmModule } =
      await import(/* @vite-ignore */ moduleUrl);
    await initClientWasm({ width: 80, height: 24 });
    const wasm = getClientWasmModule() as Record<string, unknown>;
    const diagnostics = {
      errorBinding: typeof (wasm["ErrorBinding"] as { snapshot?: unknown }),
      legacy: Object.keys(wasm).filter((name) =>
        /^_(?:errno|asciichat_errno|client_|mirror_|browser_|wasm_|set_|get_|term_renderer|peer_)/.test(
          name,
        ),
      ),
    };
    cleanupClientWasm();
    return diagnostics;
  });
  expect(diagnostics).toEqual({
    errorBinding: "function",
    legacy: [],
  });
});

for (const [mode, url] of [
  ["mirror", "/mirror?test2&playing=true"],
  ["client", "/client"],
  ["client", "/discovery"],
] as const) {
  test(`${url} loads matching versioned JavaScript and WASM`, async ({
    page,
    context,
  }) => {
    const assets = new Set<string>();
    context.on("request", (request) => {
      const path = new URL(request.url()).pathname;
      if (path.startsWith("/wasm/")) assets.add(path);
    });
    await context.route(/\/wasm\/(client|mirror)\.(js|wasm)$/, (route) =>
      route.abort(),
    );
    await page.goto(url);
    if (mode === "mirror") {
      await expect
        .poll(() =>
          page.locator("canvas.ascii-canvas").evaluate((element) => {
            const canvas = element as HTMLCanvasElement;
            return canvas
              .getContext("2d")!
              .getImageData(0, 0, canvas.width, canvas.height)
              .data.some((value, index) => index % 4 !== 3 && value > 8);
          }),
        )
        .toBe(true);
    } else {
      await expect
        .poll(() =>
          page.evaluate(async () => {
            const url = "/src/wasm/client.ts";
            return !!(
              await import(/* @vite-ignore */ url)
            ).getClientWasmModule();
          }),
        )
        .toBe(true);
    }
    expect(assets).toContain(`/wasm/${version}/${mode}.js`);
    expect(assets).toContain(`/wasm/${version}/${mode}.wasm`);
    const other = mode === "mirror" ? "client" : "mirror";
    expect(assets).not.toContain(`/wasm/${version}/${other}.js`);
    expect(
      [...assets].every((path) => path.startsWith(`/wasm/${version}/`)),
    ).toBe(true);
  });
}

for (const mode of ["mirror", "client"] as const) {
  test(`${mode} raster worker loads the same versioned artifact pair`, async ({
    page,
    context,
  }) => {
    const assets = new Set<string>();
    context.on("request", (request) => {
      const path = new URL(request.url()).pathname;
      if (path.startsWith("/wasm/")) assets.add(path);
    });
    await page.goto(`/tests/e2e/raster-worker.html?mode=${mode}`);
    await page.waitForFunction(() => "render" in window);
    const result = await page.evaluate(() =>
      (
        window as unknown as {
          render(matrix: boolean): Promise<{ lit: number }>;
        }
      ).render(false),
    );
    expect(result.lit).toBeGreaterThan(100);
    expect(assets).toContain(`/wasm/${version}/${mode}.js`);
    expect(assets).toContain(`/wasm/${version}/${mode}.wasm`);
    expect(
      [...assets].every((path) => path.startsWith(`/wasm/${version}/`)),
    ).toBe(true);
  });
}

test("discovery protocol uses owned Embind sessions", async ({ page }) => {
  await page.goto("/tests/network/discovery-protocol.html");
  await expect(page).toHaveTitle("PASS: shared C discovery protocol", {
    timeout: 30000,
  });
  await expect(page.locator("#results")).toContainText("10 tests passed");
});

test("renderer diagnostic pages use Embind", async ({ page }) => {
  for (const path of ["/wasm-render-test.html", "/wasm-block-test.html"]) {
    await page.goto(path);
    await expect(page.locator("#status")).toContainText("PASS", {
      timeout: 30000,
    });
  }
});
