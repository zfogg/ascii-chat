import {
  useEffect,
  useLayoutEffect,
  useRef,
  useSyncExternalStore,
} from "react";
import {
  getMirrorModule,
  ColorMode,
  TextRenderer,
  paletteName,
  runtimeOptions,
  type RuntimeSnapshot,
  type RuntimeValues,
} from "@ascii-chat/shared/wasm";
import type { BinarySettingsConfig } from "../components";
import {
  acknowledgeEngineSettings,
  applyMirrorWasmSettings,
} from "./useWasmOptions";

const emptySubscribe = () => () => {};
const emptySnapshot = () => null;

/** Observe changes from every C writer, including keyboard and network adapters. */
export function useEngineSettings(
  ready: boolean,
  settings: BinarySettingsConfig,
  onChange: (settings: BinarySettingsConfig) => void,
  applySettings: (
    settings: BinarySettingsConfig,
  ) => void = applyMirrorWasmSettings,
) {
  const module = ready ? getMirrorModule() : null;
  const store = module ? runtimeOptions(module) : null;
  const snapshot = useSyncExternalStore(
    store?.subscribe ?? emptySubscribe,
    store?.getSnapshot ?? emptySnapshot,
    emptySnapshot,
  );
  const previous = useRef<RuntimeSnapshot | null>(null);
  const current = useRef({ settings, onChange });
  useLayoutEffect(() => {
    current.current = { settings, onChange };
  }, [settings, onChange]);
  useLayoutEffect(() => {
    previous.current = null;
  }, [store]);
  // Commit startup inputs and user edits before observing the resulting C state.
  useLayoutEffect(() => {
    if (!ready) return;
    try {
      applySettings(settings);
    } catch (error) {
      console.error("Failed to apply renderer settings:", error);
    }
  }, [ready, settings, applySettings]);
  useEffect(() => {
    // Earlier layout/passive effects may already have published a newer revision.
    // React will render that snapshot next; do not echo the stale one into the UI.
    if (snapshot && store && snapshot !== store.getSnapshot()) return;
    const old = previous.current;
    previous.current = snapshot;
    if (!snapshot || old === snapshot) return;
    const { settings, onChange } = current.current;
    const values = snapshot.values;
    const next = { ...settings };
    const changed = (name: string) =>
      values[name] !== undefined && (!old || old.values[name] !== values[name]);
    if (store) acknowledgeEngineSettings(store, values, changed);
    const numeric = (name: string) => Number(values[name]);
    const colors: Record<number, BinarySettingsConfig["colorMode"]> = {
      [ColorMode.Auto]: "auto",
      [ColorMode.None]: "none",
      [ColorMode.Ansi16]: "16",
      [ColorMode.Ansi256]: "256",
      [ColorMode.Truecolor]: "truecolor",
    };
    const filters: BinarySettingsConfig["colorFilter"][] = [
      "none",
      "black",
      "white",
      "green",
      "magenta",
      "fuchsia",
      "orange",
      "teal",
      "cyan",
      "pink",
      "red",
      "yellow",
      "rainbow",
    ];
    if (changed("color_mode"))
      next.colorMode = colors[numeric("color_mode")] ?? "auto";
    if (
      changed("color_filter") &&
      !(
        settings.animationEnabled &&
        ["fft", "waveform"].includes(settings.animation ?? "")
      )
    )
      next.colorFilter = filters[numeric("color_filter")] ?? "none";
    if (changed("render_mode")) next.renderMode = numeric("render_mode");
    if (changed("renderer"))
      next.renderer =
        numeric("renderer") === TextRenderer.Glyph ? "glyph" : "luminance";
    if (changed("stabilize")) next.stabilize = !!values["stabilize"];
    if (changed("palette_type"))
      next.palette = paletteName(numeric("palette_type"));
    if (changed("palette_explicit"))
      next.paletteExplicit = !!values["palette_explicit"];
    if (changed("palette_custom"))
      next.paletteChars = String(values["palette_custom"]);
    if (changed("flip_x")) next.flipX = !!values["flip_x"];
    if (changed("flip_x_explicit"))
      next.flipXExplicit = !!values["flip_x_explicit"];
    if (changed("flip_y")) next.flipY = !!values["flip_y"];
    if (changed("fps")) next.targetFps = numeric("fps");
    if (["matrix_rain", "waveform", "fft"].some(changed)) {
      next.animationEnabled = !!(
        values["matrix_rain"] ||
        values["waveform"] ||
        values["fft"]
      );
      next.matrixRain = !!values["matrix_rain"];
      next.animation = values["waveform"]
        ? "waveform"
        : values["fft"]
          ? "fft"
          : "matrix";
    }
    if (
      Object.keys(next).some(
        (key) =>
          next[key as keyof BinarySettingsConfig] !==
          settings[key as keyof BinarySettingsConfig],
      )
    )
      onChange(next);
  }, [snapshot, store]);
}

/** The network adapter transmits C's committed preferences, with viewport dimensions. */
export function renderPreferences(cols: number, rows: number): RuntimeValues {
  const module = getMirrorModule();
  if (!module) throw new Error("Renderer is not ready");
  const values = runtimeOptions(module).getSnapshot().values;
  return {
    width: cols,
    height: rows,
    fps: values["fps"]!,
    color_mode:
      values["color_mode"] === ColorMode.Auto
        ? ColorMode.Truecolor
        : values["color_mode"]!,
    color_filter: values["color_filter"]!,
    render_mode: values["render_mode"]!,
    renderer: values["renderer"]!,
    stabilize: values["stabilize"]!,
    palette_type: values["palette_type"]!,
    palette_custom: values["palette_custom"]!,
    palette_explicit: values["palette_explicit"]!,
    matrix_rain: values["matrix_rain"]!,
  };
}
