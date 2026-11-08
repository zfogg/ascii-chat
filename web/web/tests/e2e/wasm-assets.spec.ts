import { expect, test } from "./browser-fixture";
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";

const version = createHash("sha256")
  .update(readFileSync("src/wasm/dist/manifest.json"))
  .digest("hex")
  .slice(0, 32);

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
    const codec = new module.OpusCodecBinding(48000, 2, 64000, 4000);
    const competingCodec = new module.OpusCodecBinding(48000, 2, 64000, 4000);
    const competingRejected = !competingCodec.valid();
    competingCodec.delete();
    const pcm = Int16Array.from({ length: 1920 }, (_, i) =>
      Math.round(5000 * Math.sin(i / 13)),
    );
    const encoded = codec.encode(pcm);
    const decoded = encoded ? codec.decode(encoded, 960) : null;
    const opus = {
      valid: codec.valid(),
      competingRejected,
      bytes: encoded?.length ?? 0,
      samples: decoded?.length ?? 0,
      invalid: codec.encode(new Int16Array(12000)),
    };
    codec.delete();

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
          const opened = server.quicPacket("open", 0, 0, sealed.packet);
          decoded = opened.packet;
          sequence = opened.sequence.toString();
        } else {
          client.sendPacket(original);
          while (toServer.length)
            decoded = server.receive(toServer.shift()!) ?? decoded;
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
      opus,
      classic: await exercise(false),
      quic: await exercise(true),
    };
  });
  expect(result.audio).toEqual({
    valid: true,
    status: 0,
    sample: 0.25,
    bad: -1,
  });
  expect(result.opus.valid).toBe(true);
  expect(result.opus.competingRejected).toBe(true);
  expect(result.opus.bytes).toBeGreaterThan(0);
  expect(result.opus.samples).toBe(1920);
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
      const allocation = m._malloc(before);
      if (!allocation) throw new Error("could not force memory growth");
      const after = m.HEAPU8.byteLength;
      m._free(allocation);
      const next = new Uint8ClampedArray(pixels.length);
      if (!renderer.copyPixels(next))
        throw new Error("pixel copy after growth failed");
      return {
        before,
        after,
        rejected: bad === undefined,
        retained: snapshot.every((byte, i) => pixels[i] === byte),
        same: snapshot.every((byte, i) => next[i] === byte),
      };
    } finally {
      renderer.delete();
      frame.delete();
      m._mirror_cleanup();
    }
  }, version);
  expect(result.after).toBeGreaterThan(result.before);
  expect(result.rejected).toBe(true);
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
      stacks: typeof wasm["_errno_print_stacks"],
      history: typeof wasm["_errno_print_history"],
      legacy: Object.keys(wasm).filter((name) =>
        name.startsWith("_asciichat_errno_"),
      ),
    };
    cleanupClientWasm();
    return diagnostics;
  });
  expect(diagnostics).toEqual({
    stacks: "function",
    history: "function",
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
