import type { SessionUiEvent } from "@ascii-chat/shared/wasm";
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
