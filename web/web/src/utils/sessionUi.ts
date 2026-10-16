import type { SessionUiEvent } from "@ascii-chat/shared/wasm";
import type {
  BinarySettingsConfig,
  ColorMode,
  ColorFilter,
} from "../components/shared/BinarySettings";

/** Read back C's authoritative settings after a keyboard command. */
export function sessionUiSettings(
  event: SessionUiEvent,
  settings: BinarySettingsConfig,
): BinarySettingsConfig {
  const m = event.module;
  const colors: ColorMode[] = ["none", "16", "256", "truecolor"];
  const filters: ColorFilter[] = [
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
  const waveform = !!m._get_waveform?.(),
    fft = !!m._get_fft?.(),
    matrix = !!m._get_matrix_rain?.();
  return {
    ...settings,
    colorMode: colors[m._get_color_mode?.() ?? -1] ?? "auto",
    colorFilter: filters[m._get_color_filter?.() ?? 0] ?? "none",
    flipX: !!m._get_flip_x?.(),
    flipY: !!m._get_flip_y?.(),
    renderMode: m._get_render_mode?.() ?? 0,
    matrixRain: matrix,
    animationEnabled: waveform || fft || matrix,
    animation: waveform ? "waveform" : fft ? "fft" : "matrix",
  };
}

export function sessionUiMedia(
  event: SessionUiEvent,
  video: HTMLVideoElement | null,
) {
  if (!video) return;
  if (["m", "M", "ArrowUp", "ArrowDown"].includes(event.key))
    video.volume = event.module._wasm_ui_volume();
  if (!video.srcObject && Number.isFinite(video.duration)) {
    if (event.action === 5 || event.action === 4)
      video.currentTime = Math.min(
        video.duration,
        Math.max(0, video.currentTime + (event.action === 4 ? -30 : 30)),
      );
    if (event.action === 6) {
      if (video.paused) void video.play().catch(console.error);
      else video.pause();
    }
  }
}
