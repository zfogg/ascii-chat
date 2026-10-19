/** Wait for a drawable frame even when autoplay requires another user gesture. */
export async function playMedia(
  video: HTMLVideoElement,
  signal: AbortSignal,
  timeoutMs = 30_000,
) {
  signal.throwIfAborted();
  let cleanup = () => {};
  let loaded = () => {};
  const ready = new Promise<void>((resolve) => {
    loaded = () => resolve();
    if (video.readyState >= 2) resolve();
  });
  const failure = new Promise<never>((_, reject) => {
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
      () =>
        reject(new DOMException("Timed out loading video.", "TimeoutError")),
      timeoutMs,
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
  });
  try {
    await Promise.race([
      failure,
      Promise.all([
        ready,
        video.play().catch((error: unknown) => {
          if (
            !(error instanceof DOMException) ||
            error.name !== "NotAllowedError"
          )
            throw error;
        }),
      ]),
    ]);
  } finally {
    cleanup();
  }
}
