import { test, expect, type Page } from "./browser-fixture";
import { fileURLToPath } from "node:url";

const moduleUrl = `/@fs/${fileURLToPath(
  new URL("../../../packages/shared/src/wasm/mirror.ts", import.meta.url),
)
  .replaceAll("\\", "/")
  .replace(/^\/+/, "")}`;

declare global {
  interface Window {
    __sessionUiFrames?: string[];
    __sessionSettingsPatches?: string[];
  }
}

async function observe(page: Page) {
  await page.goto("/mirror");
  await page.waitForFunction(() => {
    const canvas = document.querySelector<HTMLCanvasElement>(
      "canvas.ascii-canvas",
    );
    if (!canvas?.width || !canvas.height) return false;
    const pixels = canvas
      .getContext("2d")!
      .getImageData(0, 0, canvas.width, canvas.height).data;
    return pixels.some((value, index) => index % 4 !== 3 && value > 100);
  });
  await page.evaluate(async (url) => {
    const { getMirrorModule } = await import(url);
    const m = getMirrorModule();
    const patch = m._wasm_options_patch;
    window.__sessionSettingsPatches = [];
    m._wasm_options_patch = (pointer: number, revision: number) => {
      window.__sessionSettingsPatches!.push(m.UTF8ToString(pointer));
      if (window.__sessionSettingsPatches!.length > 100)
        throw new Error(
          `Settings feedback loop: ${window.__sessionSettingsPatches!.slice(-10).join("; ")}`,
        );
      return patch(pointer, revision);
    };
    const feed = m._term_renderer_feed;
    window.__sessionUiFrames = [];
    m._term_renderer_feed = (
      renderer: number,
      pointer: number,
      length: number,
    ) => {
      const text = m.UTF8ToString(pointer, length);
      window.__sessionUiFrames!.push(text);
      if (window.__sessionUiFrames!.length > 30)
        window.__sessionUiFrames!.shift();
      return feed(renderer, pointer, length);
    };
  }, moduleUrl);
  await expect
    .poll(() => lastFrame(page))
    .toContain("Video chat in your terminal");
}

async function lastFrame(page: Page) {
  return page.evaluate(() => window.__sessionUiFrames?.at(-1) ?? "");
}

test.use({ viewport: { width: 1440, height: 1000 } });

test("C splash animates and keyboard settings update both C and the web controls", async ({
  page,
}) => {
  const errors: string[] = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await observe(page);
  const canvas = page.locator("canvas.ascii-canvas");
  await canvas.focus();
  await page.keyboard.press("ArrowDown");
  await expect
    .poll(() => lastFrame(page))
    .toContain("Video chat in your terminal");
  expect(await lastFrame(page)).not.toContain("Volume DOWN");
  expect(await lastFrame(page)).not.toContain("/: search logs");
  await page.screenshot({ path: "artifacts/session-splash-startup-web.png" });
  await page.keyboard.press("]");
  await expect.poll(() => lastFrame(page)).toContain("Volume DOWN");
  await page.keyboard.press("]");
  await expect
    .poll(() => lastFrame(page))
    .toContain("Video chat in your terminal");
  expect(await lastFrame(page)).not.toContain("Volume DOWN");
  await page.keyboard.press("ArrowUp");
  const canvasFps = async () => {
    const text = await page.getByText(/^FPS:/).innerText();
    return Number(text.match(/FPS: (\d+)/)?.[1] ?? 0);
  };
  await expect.poll(canvasFps).toBeGreaterThan(0);
  await page.screenshot({ path: "artifacts/session-splash-fps-web.png" });
  const before = await canvas.screenshot();
  await expect
    .poll(async () => (await canvas.screenshot()).equals(before))
    .toBe(false);
  await canvas.focus();
  await page.keyboard.press("?");
  await expect
    .poll(() => lastFrame(page))
    .toContain("ascii-chat Keyboard Shortcuts");
  // Static help does not keep counting covered splash frames.
  await expect.poll(canvasFps).toBe(0);
  await page.keyboard.press("c");
  await expect(page).toHaveURL(/colorMode=none/);
  await expect.poll(() => lastFrame(page)).toContain("Mono");
  for (const [key, parameter, value] of [
    ["x", "horizontalFlip", "off"],
    ["y", "flipY", "true"],
    ["r", "renderMode", "1"],
    ["1", "animationEnabled", "true"],
    ["2", "animation", "waveform"],
    ["3", "animation", "fft"],
  ]) {
    await page.keyboard.press(key!);
    await expect
      .poll(() => new URL(page.url()).searchParams.get(parameter!))
      .toBe(value);
  }
  await page.keyboard.press("ArrowDown");
  await expect.poll(() => lastFrame(page)).toMatch(/(?:89|90)%/);
  await page.keyboard.press("m");
  await expect.poll(() => lastFrame(page)).toMatch(/Volume\s*:\s*\[\s*\] 0%/);
  await page.keyboard.press("m");
  await expect.poll(() => lastFrame(page)).toMatch(/(?:89|90)%/);
  await page.getByRole("button", { name: "Settings", exact: true }).click();
  await expect(
    page
      .locator("select")
      .filter({ has: page.locator('option[value="truecolor"]') }),
  ).toHaveValue("none");
  expect(
    await page.evaluate(() => window.__sessionSettingsPatches!.length),
  ).toBeLessThan(100);
  await canvas.focus();
  await page.keyboard.press("Escape");
  await expect
    .poll(() => lastFrame(page))
    .toContain("Video chat in your terminal");
  expect(errors).toEqual([]);
});

test("help owns presentation during live video and recovers after resize", async ({
  page,
}) => {
  await observe(page);
  await page.getByRole("button", { name: "Start Webcam", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Stop", exact: true }),
  ).toBeVisible({ timeout: 15_000 });
  const canvas = page.locator("canvas.ascii-canvas");
  await canvas.focus();
  await page.keyboard.press("?");
  await expect
    .poll(() => lastFrame(page))
    .toContain("ascii-chat Keyboard Shortcuts");
  await page.waitForTimeout(400);
  const frames = await page.evaluate(() => window.__sessionUiFrames!.slice(-5));
  expect(
    frames.every((frame) => frame.includes("ascii-chat Keyboard Shortcuts")),
  ).toBe(true);
  await page.setViewportSize({ width: 480, height: 480 });
  await expect.poll(() => lastFrame(page)).toContain("Terminal too small");
  await page.setViewportSize({ width: 1440, height: 1000 });
  await expect
    .poll(() => lastFrame(page))
    .toContain("ascii-chat Keyboard Shortcuts");
  await canvas.focus();
  await page.keyboard.press("Escape");
  await expect
    .poll(async () => {
      const frame = await lastFrame(page);
      return (
        frame.length > 100 &&
        !frame.includes("Keyboard Shortcuts") &&
        !frame.includes("Video chat in your terminal")
      );
    })
    .toBe(true);
});

test("compiled C runtime retains newest covered frame, bounds input and tears down repeatedly", async ({
  page,
}) => {
  await observe(page);
  const result = await page.evaluate(async (url) => {
    const { getMirrorModule } = await import(url);
    const m = getMirrorModule();
    const text = (value: string, fn: (p: number, n: number) => void) => {
      const n = m.lengthBytesUTF8(value),
        p = m._malloc(n + 1);
      try {
        m.stringToUTF8(value, p, n + 1);
        fn(p, n);
      } finally {
        m._free(p);
      }
    };
    const outcomes = [];
    for (let i = 0; i < 10; i++) {
      const s = m._wasm_ui_create();
      m._wasm_ui_resize(s, 120, 40);
      text("old", (p, n) => m._wasm_ui_media(s, p, n));
      m._wasm_ui_key(s, 63);
      const help = m.UTF8ToString(m._wasm_ui_tick(s, 100));
      text("newest", (p, n) => m._wasm_ui_media(s, p, n));
      const covered = m.UTF8ToString(m._wasm_ui_tick(s, 200));
      m._wasm_ui_key(s, 27);
      const restored = m.UTF8ToString(m._wasm_ui_tick(s, 300));
      const unchanged = m._wasm_ui_tick(s, 400);
      const invalid = m._wasm_ui_media(s, 0, 0);
      outcomes.push(
        help.includes("Keyboard Shortcuts") &&
          covered.includes("Keyboard Shortcuts") &&
          restored.endsWith("newest") &&
          unchanged === 0 &&
          invalid !== 0,
      );
      m._wasm_ui_destroy(s);
    }
    return outcomes;
  }, moduleUrl);
  expect(result).toEqual(Array(10).fill(true));
});

test("C application logs survive closed overlays and share exclusive presentation", async ({
  page,
}) => {
  await observe(page);
  const canvas = page.locator("canvas.ascii-canvas");
  await canvas.focus();
  // This runs the real C keyboard action and log_info path while logs are hidden.
  await page.keyboard.press("ArrowDown");
  await page.keyboard.press("]");
  await expect.poll(() => lastFrame(page)).toContain("/: search logs");
  await expect.poll(() => lastFrame(page)).toContain("Volume DOWN");
  expect(await lastFrame(page)).not.toContain("Video chat in your terminal");
  await page.keyboard.press("/");
  await page.keyboard.type("Volume DOWN");
  await expect.poll(() => lastFrame(page)).toContain("/Volume DOWN");
  await page.keyboard.press("Enter");
  await expect.poll(() => lastFrame(page)).toContain("Volume");
  expect(await lastFrame(page)).not.toContain("OPTIONS_CONFIG_DESTROY");
  await page.screenshot({ path: "artifacts/session-logs-search-web.png" });
  await page.keyboard.press("Escape");
  await expect.poll(() => lastFrame(page)).toContain("/: search logs");
  await page.screenshot({ path: "artifacts/session-logs-web.png" });
  await page.keyboard.press("?");
  await expect.poll(() => lastFrame(page)).toContain("Toggle application logs");
  expect(await lastFrame(page)).not.toContain("/: search logs");
  await page.screenshot({ path: "artifacts/session-logs-help-web.png" });
  await page.keyboard.press("]");
  await expect.poll(() => lastFrame(page)).toContain("/: search logs");
  await page.keyboard.press("Escape");
  await expect
    .poll(() => lastFrame(page))
    .toContain("Video chat in your terminal");
  await page.getByRole("button", { name: "Start Webcam", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Stop", exact: true }),
  ).toBeVisible({ timeout: 15_000 });
  await canvas.focus();
  await page.keyboard.press("]");
  await expect.poll(() => lastFrame(page)).toContain("/: search logs");
  await page.keyboard.press("ArrowDown");
  await expect.poll(() => lastFrame(page)).toContain("Volume DOWN");
  await page.waitForTimeout(400);
  expect(
    (await page.evaluate(() => window.__sessionUiFrames!.slice(-5))).every(
      (frame) => frame.includes("/: search logs"),
    ),
  ).toBe(true);
  await page.setViewportSize({ width: 360, height: 320 });
  await expect.poll(() => lastFrame(page)).toContain("Terminal too small");
  await page.setViewportSize({ width: 1440, height: 1000 });
  await expect.poll(() => lastFrame(page)).toContain("/: search logs");
  await canvas.focus();
  await page.keyboard.press("]");
  await expect
    .poll(async () => {
      const frame = await lastFrame(page);
      return (
        frame.length > 100 &&
        !frame.includes("/: search logs") &&
        !frame.includes("Keyboard Shortcuts") &&
        !frame.includes("Video chat in your terminal")
      );
    })
    .toBe(true);
});

test("connected web client shows its C network logs", async ({ page }) => {
  test.skip(
    !process.env["SESSION_UI_SERVER_URL"],
    "Requires a local ascii-chat WebSocket server",
  );
  await page.goto(
    `/client?connect=true&serverUrl=${encodeURIComponent(process.env["SESSION_UI_SERVER_URL"]!)}`,
  );
  await expect(
    page.getByRole("button", { name: "Disconnect", exact: true }),
  ).toBeVisible({ timeout: 30000 });
  const canvas = page.locator("canvas.ascii-canvas");
  await expect(canvas).toBeVisible();
  await page.evaluate(async (url) => {
    const { getMirrorModule } = await import(url);
    const m = getMirrorModule();
    const feed = m._term_renderer_feed;
    window.__sessionUiFrames = [];
    m._term_renderer_feed = (
      renderer: number,
      pointer: number,
      length: number,
    ) => {
      window.__sessionUiFrames!.push(m.UTF8ToString(pointer, length));
      if (window.__sessionUiFrames!.length > 30)
        window.__sessionUiFrames!.shift();
      return feed(renderer, pointer, length);
    };
  }, moduleUrl);
  await canvas.focus();
  await page.keyboard.press("]");
  await expect.poll(() => lastFrame(page)).toContain("/: search logs");
  await page.keyboard.press("ArrowDown");
  await expect.poll(() => lastFrame(page)).toContain("Volume DOWN");
  await expect.poll(() => lastFrame(page)).toContain("Handshake complete");
  const canvasFps = async () =>
    Number(
      (await page.getByText(/^FPS:/).innerText()).match(/FPS: (\d+)/)?.[1] ?? 0,
    );
  await expect.poll(canvasFps).toBeGreaterThan(0);
  await page.screenshot({ path: "artifacts/session-logs-web-client.png" });
  await page.keyboard.press("?");
  await expect.poll(() => lastFrame(page)).toContain("Toggle application logs");
  await page.keyboard.press("]");
  await expect.poll(() => lastFrame(page)).toContain("/: search logs");
  await page.keyboard.press("Escape");
  await expect
    .poll(async () => !(await lastFrame(page)).includes("/: search logs"))
    .toBe(true);
  await expect(
    page.getByRole("button", { name: "Disconnect", exact: true }),
  ).toBeVisible();
});

test("C owns automatic horizontal orientation and the browser echoes it without selecting it", async ({
  page,
}) => {
  await observe(page);
  await page.getByRole("button", { name: "Settings", exact: true }).click();
  const horizontal = page.getByRole("button", { name: /Flip Horizontal/ });
  const canvas = page.locator("canvas.ascii-canvas");
  const state = () =>
    page.evaluate(async (url) => {
      const { getMirrorModule } = await import(url);
      const m = getMirrorModule();
      const p = m._wasm_options_values();
      try {
        return JSON.parse(m.UTF8ToString(p)).values;
      } finally {
        m._wasm_options_release(p);
      }
    }, moduleUrl);
  const orientation = async (flip: boolean, explicit = false) => {
    await expect(horizontal).toHaveAttribute("aria-pressed", String(flip));
    await expect
      .poll(async () => {
        const values = await state();
        return [values.flip_x, values.flip_x_explicit];
      })
      .toEqual([flip, explicit]);
  };
  const key = async (value: string) => {
    await canvas.focus();
    await page.keyboard.press(value);
  };
  await orientation(true);
  await key("2");
  await orientation(false);
  await key("3");
  await orientation(false);
  await key("3");
  await orientation(true);
  expect(new URL(page.url()).searchParams.has("horizontalFlip")).toBe(false);
  await page.getByRole("button", { name: "Start Webcam", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Stop", exact: true }),
  ).toBeVisible();
  await orientation(true);
  await page.getByRole("button", { name: "Upload File", exact: true }).click();
  await page
    .getByLabel("Choose media file")
    .setInputFiles("tests/fixtures/media/still.png");
  await orientation(false);
  await page.getByRole("button", { name: "Toggle Media", exact: true }).click();
  await orientation(true);
  await page.getByRole("button", { name: "Toggle Media", exact: true }).click();
  await orientation(false);
  // Choose on while viewing media; the choice applies to every subsequent source.
  await horizontal.click();
  await orientation(true, true);
  await page.getByRole("button", { name: "Toggle Media", exact: true }).click();
  await key("2");
  await orientation(true, true);
  await key("3");
  await orientation(true, true);
  await horizontal.click();
  await orientation(false, true);
  await key("3");
  await orientation(false, true);
  await page.getByRole("button", { name: "Toggle Media", exact: true }).click();
  await orientation(false, true);
  await key("x");
  await orientation(true, true);
  await key("x");
  await orientation(false, true);
  await page.screenshot({ path: "artifacts/flip-c-policy-web.png" });
  // A fresh program without a saved explicit choice starts with automatic defaults.
  await page.goto("/mirror?animationEnabled=true&animation=waveform");
  await page.getByRole("button", { name: "Settings", exact: true }).click();
  await orientation(false);
  await key("2");
  await orientation(true);
});

for (const mode of ["client", "discovery"]) {
  test(`${mode}: horizontal orientation follows uploaded media and shares an explicit choice`, async ({
    page,
  }) => {
    await page.goto(`/${mode}`);
    await page.getByRole("button", { name: "Settings", exact: true }).click();
    const horizontal = page.getByRole("button", { name: /Flip Horizontal/ });
    const state = () =>
      page.evaluate(async (url) => {
        const { getMirrorModule } = await import(url);
        const m = getMirrorModule();
        if (!m) return null;
        const p = m._wasm_options_values();
        try {
          const values = JSON.parse(m.UTF8ToString(p)).values;
          return [values.flip_x, values.flip_x_explicit];
        } finally {
          m._wasm_options_release(p);
        }
      }, moduleUrl);
    await expect(horizontal).toHaveAttribute("aria-pressed", "true");
    await page
      .getByRole("button", { name: "Upload File", exact: true })
      .click();
    await page
      .getByLabel("Choose media file")
      .setInputFiles("tests/fixtures/media/still.png");
    await expect.poll(state).toEqual([false, false]);
    await expect(horizontal).toHaveAttribute("aria-pressed", "false");
    await horizontal.click();
    await expect.poll(state).toEqual([true, true]);
    await page.getByRole("button", { name: "Stop", exact: true }).click();
    await expect.poll(state).toEqual([true, true]);
    await horizontal.click();
    await expect.poll(state).toEqual([false, true]);
    await page.screenshot({ path: `artifacts/flip-c-policy-${mode}.png` });
  });
}

test("horizontal orientation startup commits saved choices before reading C", async ({
  page,
}) => {
  for (const [query, flip] of [
    ["animationEnabled=true&animation=waveform&horizontalFlip=on", true],
    ["horizontalFlip=off", false],
  ] as const) {
    await page.goto(`/mirror?${query}`);
    await page.getByRole("button", { name: "Settings", exact: true }).click();
    const state = () =>
      page.evaluate(async (url) => {
        const { getMirrorModule } = await import(url);
        const m = getMirrorModule();
        if (!m) return null;
        const p = m._wasm_options_values();
        try {
          const values = JSON.parse(m.UTF8ToString(p)).values;
          return [values.flip_x, values.flip_x_explicit];
        } finally {
          m._wasm_options_release(p);
        }
      }, moduleUrl);
    await expect.poll(state).toEqual([flip, true]);
    await expect(
      page.getByRole("button", { name: /Flip Horizontal/ }),
    ).toHaveAttribute("aria-pressed", String(flip));
    await page.locator("canvas.ascii-canvas").focus();
    await page.keyboard.press("2");
    await expect.poll(state).toEqual([flip, true]);
  }
});
