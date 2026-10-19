/** Bound device startup and release streams returned after cancellation or timeout. */
export function acquireMediaStream(
  constraints: MediaStreamConstraints,
  signal: AbortSignal,
): Promise<MediaStream> {
  signal.throwIfAborted();
  return new Promise((resolve, reject) => {
    let settled = false;
    let permission: PermissionStatus | undefined;
    const timeout = () => {
      if (settled) return;
      settled = true;
      cleanup();
      reject(
        new DOMException(
          "Timed out starting the selected device.",
          "AbortError",
        ),
      );
    };
    // Allow time for a permission decision; known grants use a shorter startup limit.
    let timer = window.setTimeout(timeout, 30_000);
    const permissionChanged = () => {
      if (settled || permission?.state !== "granted") return;
      clearTimeout(timer);
      timer = window.setTimeout(timeout, 10_000);
    };
    const cleanup = () => {
      clearTimeout(timer);
      signal.removeEventListener("abort", aborted);
      permission?.removeEventListener("change", permissionChanged);
    };
    const aborted = () => {
      if (settled) return;
      settled = true;
      cleanup();
      reject(new DOMException("Device preview cancelled", "AbortError"));
    };
    signal.addEventListener("abort", aborted, { once: true });
    if (navigator.permissions?.query) {
      void navigator.permissions
        .query({
          name: (constraints.video ? "camera" : "microphone") as PermissionName,
        })
        .then((status) => {
          if (settled) return;
          permission = status;
          permission.addEventListener("change", permissionChanged);
          permissionChanged();
        })
        .catch(() => undefined);
    }
    navigator.mediaDevices.getUserMedia(constraints).then(
      (stream) => {
        if (settled) {
          stream.getTracks().forEach((track) => track.stop());
          return;
        }
        settled = true;
        cleanup();
        resolve(stream);
      },
      (cause: unknown) => {
        if (settled) return;
        settled = true;
        cleanup();
        reject(cause);
      },
    );
  });
}
