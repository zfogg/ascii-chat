import { expect, test } from "./browser-fixture";
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";

const version = createHash("sha256")
  .update(readFileSync("src/wasm/dist/manifest.json"))
  .digest("hex")
  .slice(0, 32);

test("binding failures record actionable C errors", async ({ page }) => {
  await page.goto("/client");
  const errors = await page.evaluate(async () => {
    const moduleUrl = "/src/wasm/client.ts";
    const { initClientWasm, getClientWasmModule } = await import(
      /* @vite-ignore */ moduleUrl
    );
    await initClientWasm();
    const m =
      getClientWasmModule() as unknown as import("../../../packages/shared/src/wasm/generated/client-bindings").MainModule;
    const frame = new m.MirrorFrameBinding();
    const processor = new m.AudioProcessorBinding(48000, true, 16);
    const renderer = new m.TerminalRendererBinding(2, 2, 12, false, 0);
    const snapshot = (run: () => unknown) => {
      m.browser_error_dismiss();
      run();
      const count = m.errorSnapshot();
      return count ? m.errorFrame(0) : null;
    };
    try {
      return [
        snapshot(() => frame.convert(new Uint8Array(1), 1, 1, false)),
        snapshot(() => frame.transform(new Uint8Array(1), 1, 1)),
        snapshot(() =>
          processor.process(new Float32Array(4), new Float32Array(1)),
        ),
        snapshot(() => renderer.copyPixels(new Uint8ClampedArray(0))),
        snapshot(() => m.faceDetect(new Uint8ClampedArray(1), 1, 1, 0)),
        snapshot(() => m.sendClientFrame(new Uint8Array(1), 1, 1)),
        snapshot(() => m.handleHandshake(99, new Uint8Array())),
        snapshot(() => m.parseKey(99, false, new Uint8Array())),
        snapshot(() =>
          m.handoffEncode(
            1,
            new Uint8Array(1),
            new Uint8Array(16),
            0,
            0,
            0,
            0,
            0,
          ),
        ),
      ];
    } finally {
      m.browser_error_dismiss();
      renderer.delete();
      processor.delete();
      frame.delete();
    }
  });
  expect(errors).toHaveLength(9);
  for (const error of errors) {
    expect(error?.code).toBe(86);
    expect(error?.message).toBeTruthy();
  }
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
      PeerCryptoSession,
      serializePacket,
      PacketType,
    } = await import(/* @vite-ignore */ moduleUrl);
    await initClientWasm();
    const module = getClientWasmModule();
    const dsp = new module.AudioProcessorBinding(48000, true, 1920);
    const input = new Float32Array(1920).fill(0.25);
    const output = new Float32Array(input.length);
    const audio = {
      valid: dsp.valid(),
      status: dsp.process(input, output),
      sample: output[0],
      bad: dsp.process(input, new Float32Array(1)),
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
    const competingRejected = !competingCodec.valid();
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
      competingRejected,
      bytes: encoded?.length ?? 0,
      samples: decoded?.length ?? 0,
      invalid: codec.encode(new Int16Array(12000)),
    };
    codec.delete();
    const retainedAfterDelete = {
      encoded: Array.from(encoded ?? []),
      decoded: Array.from(decoded ?? []),
    };
    module.configureCrypto(
      new Uint8Array(),
      new Uint8Array(3),
      new Uint8Array(),
    );
    const invalidPeer = new module.PeerCryptoBinding(
      false,
      1,
      new Uint8Array(),
      new Uint8Array(3),
      new Uint8Array(),
    );
    const invalidPeerError = invalidPeer.error();
    invalidPeer.delete();
    module.browser_error_dismiss();
    const invalidConfig = module.configureCrypto(
      new Uint8Array(),
      new Uint8Array(3),
      new Uint8Array(),
    );
    module.errorSnapshot();
    const invalidConfigError = module.errorFrame(0).message;
    module.browser_error_dismiss();

    async function exercise(quic: boolean) {
      const toClient: Uint8Array[] = [],
        toServer: Uint8Array[] = [];
      const client = await PeerCryptoSession.create(
        { password: "password-for-interop-test" },
        true,
        false,
        (p: Uint8Array) => toServer.push(p),
      );
      const server = await PeerCryptoSession.create(
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
          throw new Error("Peer handshake did not finish");
        const original = serializePacket(PacketType.PING, new Uint8Array(8), 0);
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
  });
  expect(result.monoStatus).toBe(0);
  expect(result.monoSample).toBe(0.125);
  expect(result.opus.valid).toBe(true);
  expect(result.opus.competingRejected).toBe(true);
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
    if (m.initMirror("mirror")) throw new Error("mirror initialization failed");
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
    const renderer = new m.TerminalRendererBinding(20, 10, 16, false, 1);
    if (!renderer.valid())
      throw new Error(`renderer creation failed: ${renderer.error()}`);
    try {
      if (renderer.feed("memory growth test"))
        throw new Error("renderer feed failed");
      const pixels = new Uint8ClampedArray(
        renderer.width() * renderer.height() * 4,
      );
      if (!renderer.copyPixels(pixels)) throw new Error("pixel copy failed");
      const snapshot = Array.from(pixels);
      const before = m.HEAPU8.byteLength;
      const largeFrame = new Uint8Array(before);
      if (frame.transform(largeFrame, before / 4, 1))
        throw new Error("could not force memory growth");
      const after = m.HEAPU8.byteLength;
      const next = new Uint8ClampedArray(pixels.length);
      if (!renderer.copyPixels(next))
        throw new Error("pixel copy after growth failed");
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
      m.mirror_cleanup();
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
      errorSnapshot: typeof wasm["errorSnapshot"],
      errorFrame: typeof wasm["errorFrame"],
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
    errorSnapshot: "function",
    errorFrame: "function",
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
      await page.waitForFunction(
        () =>
          !!(window as unknown as { asciiChatWasm?: unknown }).asciiChatWasm,
      );
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
  await expect(page.locator("#results")).toContainText("8 tests passed");
});

test("renderer diagnostic pages use Embind", async ({ page }) => {
  for (const path of ["/wasm-render-test.html", "/wasm-block-test.html"]) {
    await page.goto(path);
    await expect(page.locator("#status")).toContainText("PASS", {
      timeout: 30000,
    });
  }
});
