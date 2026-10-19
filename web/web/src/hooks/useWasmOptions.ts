import {
  getMirrorModule,
  runtimeOptions,
  isOptionsInitialized,
  isWasmReady,
  type RuntimeOptionsStore,
  type RuntimeValues,
} from "@ascii-chat/shared/wasm";
import type { BinarySettingsConfig } from "../components";
import { mapColorFilterToWasm, mapColorModeToWasm } from "../utils";

// Compare UI submissions with prior submissions, so an unrelated UI edit cannot
// overwrite an engine change that React has not observed yet.
const submitted = new WeakMap<RuntimeOptionsStore, RuntimeValues>();

/** A C change observed by React is already committed, even before its UI echo. */
export function acknowledgeEngineSettings(
  store: RuntimeOptionsStore,
  values: RuntimeValues,
  changed: (name: string) => boolean,
) {
  const previous = submitted.get(store);
  if (!previous) return;
  const animations = ["matrix_rain", "waveform", "fft"];
  const animationChanged = animations.some(changed);
  const next = { ...previous };
  for (const name of Object.keys(previous)) {
    if (changed(name) || (animationChanged && animations.includes(name)))
      next[name] = values[name]!;
  }
  submitted.set(store, next);
}

export function applyMirrorWasmSettings(
  settings: BinarySettingsConfig,
): boolean {
  if (!isWasmReady() || !isOptionsInitialized()) return false;
  const module = getMirrorModule();
  if (!module) return false;
  const store = runtimeOptions(module);
  const animation = settings.animation ?? "matrix";
  const enabled = settings.animationEnabled ?? settings.matrixRain ?? false;
  const visualization =
    enabled && (animation === "waveform" || animation === "fft");
  const palettes = {
    standard: 1,
    blocks: 2,
    digital: 3,
    minimal: 4,
    cool: 5,
    custom: 6,
  };
  const values: RuntimeValues = {
    color_mode: mapColorModeToWasm(settings.colorMode),
    color_filter: mapColorFilterToWasm(
      visualization ? "none" : settings.colorFilter,
    ),
    render_mode: settings.renderMode ?? 0,
    palette_type: palettes[settings.palette],
    palette_custom: settings.paletteChars || " =#░░▒▒▓▓██",
    matrix_rain: enabled && animation === "matrix",
    waveform: enabled && animation === "waveform",
    fft: enabled && animation === "fft",
    flip_x: settings.flipX ?? false,
    flip_y: settings.flipY ?? false,
    fps: settings.targetFps,
  };
  const previous = submitted.get(store);
  const patch = Object.fromEntries(
    Object.entries(values).filter(
      ([name, value]) => previous?.[name] !== value,
    ),
  );
  // Store the submission before notifying React through the C store.
  // Notifications can synchronously re-enter a settings effect.
  submitted.set(store, values);
  try {
    if (Object.keys(patch).length) store.applyPatch(patch);
  } catch (error) {
    if (previous) submitted.set(store, previous);
    else submitted.delete(store);
    throw error;
  }
  return true;
}

export function setMirrorWasmDimensions(cols: number, rows: number): boolean {
  if (!isWasmReady() || !isOptionsInitialized()) return false;
  const module = getMirrorModule();
  if (!module) return false;
  runtimeOptions(module).applyPatch({ width: cols, height: rows });
  return true;
}
