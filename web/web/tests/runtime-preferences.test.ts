import { afterEach, describe, expect, it, vi } from "vite-plus/test";
import { act, renderHook } from "@testing-library/react";
import { useLayoutEffect } from "react";
import { useEngineSettings } from "../src/hooks/useEngineSettings";
import { runtimeOptions } from "@ascii-chat/shared/wasm";
import type { RuntimeOptionsModule } from "@ascii-chat/shared/wasm";
import { DEFAULT_SETTINGS } from "../src/utils/defaultSettings";

const engine = vi.hoisted(() => ({
  module: null as RuntimeOptionsModule | null,
}));
vi.mock("@ascii-chat/shared/wasm", async (original) => ({
  ...(await original<typeof import("@ascii-chat/shared/wasm")>()),
  getMirrorModule: () => engine.module,
  isWasmReady: () => true,
  isOptionsInitialized: () => true,
}));
import {
  applyMirrorWasmSettings,
  acknowledgeEngineSettings,
} from "../src/hooks/useWasmOptions";

afterEach(() => vi.unstubAllGlobals());

function createEngine(initial: Record<string, string | boolean | number> = {}) {
  let revision = 1;
  let values: Record<string, string | boolean | number> = { ...initial };
  let pointer = 0;
  const strings = new Map<number, string>();
  const patches: Record<string, unknown>[] = [];
  const release = (p: number) => {
    strings.delete(p);
  };
  engine.module = {
    _wasm_options_revision: () => revision,
    _wasm_options_values: () => {
      strings.set(++pointer, JSON.stringify({ revision, values }));
      return pointer;
    },
    _wasm_options_patch: (p) => {
      const patch = JSON.parse(strings.get(p)!) as Record<
        string,
        string | boolean | number
      >;
      patches.push(patch);
      values = { ...values, ...patch };
      revision++;
      return 0;
    },
    _wasm_options_release: release,
    _malloc: () => ++pointer,
    _free: release,
    UTF8ToString: (p) => strings.get(p)!,
    stringToUTF8: (value, p) => {
      strings.set(p, value);
    },
    lengthBytesUTF8: (value) => new TextEncoder().encode(value).length,
  };
  return {
    patches,
    change: () => {
      values["flip_x"] = !values["flip_x"];
      values["flip_x_explicit"] = true;
      revision++;
    },
    values: () => values,
  };
}

describe("browser preference transactions", () => {
  it("logs rejected settings and keeps the hook usable for a subsequent edit", () => {
    vi.stubGlobal("requestAnimationFrame", () => 1);
    vi.stubGlobal("cancelAnimationFrame", () => {});
    createEngine();
    const error = new Error("Settings update rejected");
    const log = vi.spyOn(console, "error").mockImplementation(() => {});
    const apply = vi.fn().mockImplementationOnce(() => {
      throw error;
    });
    try {
      const hook = renderHook(
        ({ settings }) => useEngineSettings(true, settings, vi.fn(), apply),
        { initialProps: { settings: DEFAULT_SETTINGS } },
      );
      expect(log).toHaveBeenCalledExactlyOnceWith(
        "Failed to apply renderer settings:",
        error,
      );
      hook.rerender({ settings: { ...DEFAULT_SETTINGS, targetFps: 30 } });
      expect(apply).toHaveBeenCalledTimes(2);
      expect(apply).toHaveBeenLastCalledWith(
        expect.objectContaining({ targetFps: 30 }),
      );
      hook.unmount();
    } finally {
      log.mockRestore();
    }
  });
  it("commits startup inputs before projecting the complete first C snapshot", () => {
    vi.stubGlobal("requestAnimationFrame", () => 1);
    vi.stubGlobal("cancelAnimationFrame", () => {});
    const c = createEngine({
      flip_x: false,
      flip_x_explicit: false,
      color_mode: 0,
      fps: 15,
    });
    const onChange = vi.fn();
    const hook = renderHook(() =>
      useEngineSettings(true, DEFAULT_SETTINGS, onChange),
    );
    expect(c.patches).toHaveLength(1);
    expect(c.patches[0]).not.toHaveProperty("flip_x");
    expect(onChange).toHaveBeenCalledExactlyOnceWith(
      expect.objectContaining({
        flipX: false,
        flipXExplicit: false,
        colorMode: "truecolor",
        targetFps: 60,
      }),
    );
    hook.unmount();
  });
  it("commits an explicit startup orientation before the initial C readback", () => {
    vi.stubGlobal("requestAnimationFrame", () => 1);
    vi.stubGlobal("cancelAnimationFrame", () => {});
    const c = createEngine({ flip_x: false, flip_x_explicit: true });
    const settings = { ...DEFAULT_SETTINGS, flipX: true, flipXExplicit: true };
    const onChange = vi.fn();
    const hook = renderHook(() => useEngineSettings(true, settings, onChange));
    expect(c.values()["flip_x"]).toBe(true);
    expect(onChange).not.toHaveBeenCalled();
    hook.unmount();
  });
  it("does not replay an observed animation over a newer C keyboard change", () => {
    vi.stubGlobal("requestAnimationFrame", () => 1);
    vi.stubGlobal("cancelAnimationFrame", () => {});
    const c = createEngine();
    applyMirrorWasmSettings(DEFAULT_SETTINGS);
    const store = runtimeOptions(engine.module!);
    const onChange = vi.fn();
    const hook = renderHook(() =>
      useEngineSettings(true, DEFAULT_SETTINGS, onChange),
    );
    act(() => store.applyPatch({ matrix_rain: true }));
    const observed = onChange.mock.calls.at(-1)![0];
    hook.unmount();
    store.applyPatch({ matrix_rain: false, waveform: true });
    const count = c.patches.length;
    applyMirrorWasmSettings(observed);
    expect(c.patches).toHaveLength(count);
    expect(c.values()["waveform"]).toBe(true);
    expect(c.values()["matrix_rain"]).toBe(false);
  });
  it("does not echo a snapshot superseded by an earlier React effect", () => {
    vi.stubGlobal("requestAnimationFrame", () => 1);
    vi.stubGlobal("cancelAnimationFrame", () => {});
    createEngine();
    applyMirrorWasmSettings(DEFAULT_SETTINGS);
    const store = runtimeOptions(engine.module!);
    const onChange = vi.fn();
    const hook = renderHook(
      ({ replace }) => {
        useLayoutEffect(() => {
          if (replace) store.applyPatch({ matrix_rain: false, waveform: true });
        }, [replace]);
        useEngineSettings(true, DEFAULT_SETTINGS, onChange);
      },
      { initialProps: { replace: false } },
    );
    act(() => {
      store.applyPatch({ matrix_rain: true });
      hook.rerender({ replace: true });
    });
    expect(onChange).toHaveBeenCalledTimes(1);
    expect(onChange).toHaveBeenCalledWith(
      expect.objectContaining({
        animationEnabled: true,
        animation: "waveform",
        matrixRain: false,
      }),
    );
    hook.unmount();
  });
  it("does not repeat a submission when store notification re-enters the UI adapter", () => {
    vi.stubGlobal("requestAnimationFrame", () => 1);
    vi.stubGlobal("cancelAnimationFrame", () => {});
    const c = createEngine();
    const unsubscribe = runtimeOptions(engine.module!).subscribe(() =>
      applyMirrorWasmSettings(DEFAULT_SETTINGS),
    );
    applyMirrorWasmSettings(DEFAULT_SETTINGS);
    expect(c.patches).toHaveLength(1);
    unsubscribe();
  });
  it("preserves an intervening C keyboard change when another UI field changes", () => {
    const c = createEngine();
    const settings = { ...DEFAULT_SETTINGS, flipX: true, flipXExplicit: true };
    expect(applyMirrorWasmSettings(settings)).toBe(true);
    c.change();
    applyMirrorWasmSettings({ ...settings, targetFps: 30 });
    expect(c.patches.at(-1)).toEqual({ fps: 30 });
    expect(c.values()["flip_x"]).toBe(false);
  });
  it("does not submit a C keyboard orientation back as a browser edit", () => {
    const c = createEngine({ flip_x: true, flip_x_explicit: false });
    applyMirrorWasmSettings(DEFAULT_SETTINGS);
    c.change();
    const store = runtimeOptions(engine.module!);
    const values = store.getSnapshot().values;
    acknowledgeEngineSettings(store, values, (name) =>
      name.startsWith("flip_x"),
    );
    const count = c.patches.length;
    applyMirrorWasmSettings({
      ...DEFAULT_SETTINGS,
      flipX: false,
      flipXExplicit: true,
    });
    expect(c.patches).toHaveLength(count);
  });
  it("switches animation and palette settings in a single transaction", () => {
    const c = createEngine();
    applyMirrorWasmSettings({
      ...DEFAULT_SETTINGS,
      palette: "custom",
      paletteChars: " .+#",
      animation: "fft",
      animationEnabled: true,
    });
    expect(c.patches).toHaveLength(1);
    expect(c.patches[0]).toMatchObject({
      palette_type: 6,
      palette_custom: " .+#",
      fft: true,
      waveform: false,
      matrix_rain: false,
    });
  });
});
