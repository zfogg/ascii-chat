import {
  useEffect,
  useLayoutEffect,
  useRef,
  useSyncExternalStore,
} from "react";
import {
  getMirrorModule,
  runtimeOptions,
  type RuntimeSnapshot,
  type RuntimeValues,
} from "@ascii-chat/shared/wasm";
import type { BinarySettingsConfig } from "../components";

const emptySubscribe = () => () => {};
const emptySnapshot = () => null;

/** Observe changes from every C writer, including keyboard and network adapters. */
export function useEngineSettings(
  ready: boolean,
  settings: BinarySettingsConfig,
  onChange: (settings: BinarySettingsConfig) => void,
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
  useEffect(() => {
    const old = previous.current;
    previous.current = snapshot;
    if (!old || !snapshot || old === snapshot) return;
    const { settings, onChange } = current.current;
    const values = snapshot.values;
    const next = { ...settings };
    const changed = (name: string) => old.values[name] !== values[name];
    const numeric = (name: string) => Number(values[name]);
    const colors: BinarySettingsConfig["colorMode"][] = [
      "none",
      "16",
      "256",
      "truecolor",
    ];
    const palettes: BinarySettingsConfig["palette"][] = [
      "standard",
      "standard",
      "blocks",
      "digital",
      "minimal",
      "cool",
      "custom",
    ];
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
    if (changed("palette_type"))
      next.palette = palettes[numeric("palette_type")] ?? "standard";
    if (changed("palette_custom"))
      next.paletteChars = String(values["palette_custom"]);
    if (changed("flip_x")) next.flipX = !!values["flip_x"];
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
  }, [snapshot]);
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
    color_mode: values["color_mode"] === -1 ? 3 : values["color_mode"]!,
    color_filter: values["color_filter"]!,
    render_mode: values["render_mode"]!,
    palette_type: values["palette_type"]!,
    palette_custom: values["palette_custom"]!,
    matrix_rain: values["matrix_rain"]!,
  };
}
