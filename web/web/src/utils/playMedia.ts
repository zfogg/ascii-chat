/** Wait for a drawable frame even when autoplay requires another user gesture. */
export async function playMedia(video: HTMLVideoElement, signal: AbortSignal) {
  signal.throwIfAborted();
  let cleanup = () => {};
  const ready = new Promise<void>((resolve, reject) => {
    const loaded = () => resolve();
    const failed = () =>
      reject(
        new DOMException(
          "The browser cannot decode this video.",
          "NotSupportedError",
        ),
      );
    const aborted = () =>
      reject(new DOMException("Playback cancelled", "AbortError"));
    const timer = window.setTimeout(
      () => reject(new Error("Timed out loading video.")),
      30_000,
    );
    cleanup = () => {
      clearTimeout(timer);
      video.removeEventListener("loadeddata", loaded);
      video.removeEventListener("error", failed);
      signal.removeEventListener("abort", aborted);
    };
    video.addEventListener("loadeddata", loaded);
    video.addEventListener("error", failed);
    signal.addEventListener("abort", aborted, { once: true });
    if (video.readyState >= 2) resolve();
  });
  try {
    await Promise.all([
      ready,
      video.play().catch((error: unknown) => {
        if (
          !(error instanceof DOMException) ||
          error.name !== "NotAllowedError"
        )
          throw error;
      }),
    ]);
  } finally {
    cleanup();
  }
}
