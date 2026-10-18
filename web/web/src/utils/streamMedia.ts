import coreURL from "@ffmpeg/core?url";
import wasmURL from "@ffmpeg/core/wasm?url";
import {
  bufferedAhead,
  nativeAudioSupported,
  startupBuffer,
  type MediaProbe,
} from "./mediaBuffer";
import { playMedia } from "./playMedia";

export interface StreamPlayback {
  togglePause: () => void;
  restart: () => void;
}

/** Stream one continuous mux into MSE, retaining the completed file for loops. */
export function streamMedia(
  file: File,
  video: HTMLVideoElement,
  signal: AbortSignal,
  status: (message: string) => void,
  allowNative: boolean,
  onPaused: (paused: boolean) => void,
  onError: (error: unknown) => void,
): Promise<StreamPlayback | File | null> {
  const videoCodec = "avc1.42C02A";
  if (
    typeof MediaSource === "undefined" ||
    !MediaSource.isTypeSupported(`video/mp4; codecs="${videoCodec},mp4a.40.2"`)
  )
    return Promise.resolve(null);
  if (file.size > 256 * 1024 * 1024)
    return Promise.reject(
      new Error(
        "Browser conversion is limited to 256 MB input. Trim or compress this file first.",
      ),
    );
  signal.throwIfAborted();
  return new Promise((resolve, reject) => {
    const worker = new Worker(
      new URL("./streamMedia.worker.ts", import.meta.url),
      { type: "module" },
    );
    const media = new MediaSource();
    let buffer: SourceBuffer | undefined;
    let url = "";
    let duration = 0;
    let encodingStart = 0;
    let finished = false;
    let settled = false;
    let stopped = false;
    let starting = false;
    let started = false;
    let buffering = true;
    let wantsPlay = true;
    let streaming = true;
    const queue: Uint8Array<ArrayBuffer>[] = [];
    const parts: Uint8Array<ArrayBuffer>[] = [];
    const listeners = new AbortController();
    const cleanup = () => {
      stopped = true;
      worker.terminate();
      listeners.abort();
      signal.removeEventListener("abort", abort);
      URL.revokeObjectURL(url);
      queue.length = 0;
      parts.length = 0;
    };
    const fail = (error: unknown) => {
      if (stopped) return;
      cleanup();
      if (!settled) reject(error);
      else onError(error);
    };
    const abort = () =>
      fail(new DOMException("Playback cancelled", "AbortError"));
    signal.addEventListener("abort", abort, { once: true });
    const play = () => {
      void video.play().catch((error: unknown) => {
        if (stopped) return;
        if (error instanceof DOMException && error.name === "NotAllowedError") {
          wantsPlay = false;
          onPaused(true);
        } else if (
          !(error instanceof DOMException && error.name === "AbortError")
        )
          fail(error);
      });
    };
    const controls: StreamPlayback = {
      togglePause() {
        wantsPlay = !wantsPlay;
        onPaused(!wantsPlay);
        if (!wantsPlay) video.pause();
        else check();
      },
      restart() {
        video.currentTime = 0;
        wantsPlay = true;
        onPaused(false);
        if (finished) replay();
        else {
          buffering = true;
          video.pause();
          check();
        }
      },
    };
    const replay = () => {
      if (streaming) {
        streaming = false;
        URL.revokeObjectURL(url);
        url = URL.createObjectURL(new Blob(parts, { type: "video/mp4" }));
        video.src = url;
        video.loop = true;
        queue.length = 0;
        parts.length = 0;
      }
      if (wantsPlay) play();
    };
    const check = () => {
      if (stopped || starting) return;
      if (!streaming) {
        if (wantsPlay && video.paused) play();
        return;
      }
      const ahead = bufferedAhead(video.buffered, video.currentTime);
      const end = buffer?.buffered.length
        ? buffer.buffered.end(buffer.buffered.length - 1)
        : 0;
      const elapsed = Math.max(
        0.01,
        (performance.now() - encodingStart) / 1000,
      );
      const target = finished
        ? 0
        : startupBuffer(
            Math.max(0, duration - video.currentTime),
            end,
            elapsed,
          );
      if (!finished)
        status(
          `${buffering ? "Buffering" : "Converting"}: ${ahead.toFixed(1)}s ready${buffering ? ` / ${target.toFixed(1)}s needed` : ""}`,
        );
      if (finished) status("");
      if (!started && end > 0 && (finished || ahead >= target)) {
        starting = true;
        void playMedia(video, signal)
          .then(() => {
            if (stopped) return;
            starting = false;
            started = true;
            buffering = false;
            wantsPlay = !video.paused;
            onPaused(!wantsPlay);
            settled = true;
            resolve(controls);
            check();
          })
          .catch(fail);
      } else if (started && wantsPlay) {
        if (finished && ahead <= 0) {
          replay();
          return;
        }
        if (!finished && ahead < 0.75) {
          buffering = true;
          video.pause();
        }
        if (buffering && (finished || ahead >= target)) {
          buffering = false;
          play();
        } else if (!buffering && video.paused) play();
      }
    };
    const pump = () => {
      if (stopped || !buffer || buffer.updating || media.readyState !== "open")
        return;
      if (queue.length) {
        try {
          buffer.appendBuffer(queue[0]!);
          queue.shift();
        } catch (error) {
          if (
            error instanceof DOMException &&
            error.name === "QuotaExceededError" &&
            video.currentTime > 15
          ) {
            buffer.remove(0, video.currentTime - 10);
          } else fail(error);
        }
      } else if (finished) {
        try {
          media.endOfStream();
          check();
        } catch (error) {
          fail(error);
        }
      }
    };
    video.addEventListener("timeupdate", check, { signal: listeners.signal });
    video.addEventListener(
      "waiting",
      () => {
        if (started && wantsPlay && !finished) {
          buffering = true;
          video.pause();
          check();
        }
      },
      { signal: listeners.signal },
    );
    video.addEventListener(
      "ended",
      () => {
        if (finished) replay();
      },
      { signal: listeners.signal },
    );
    video.addEventListener(
      "error",
      () => {
        if (url)
          fail(new Error("The browser could not decode the converted stream."));
      },
      { signal: listeners.signal },
    );
    worker.onerror = (event) =>
      fail(new Error(event.message || "Video converter failed."));
    worker.onmessage = (
      event: MessageEvent<{
        type: "probe" | "encoding" | "data" | "done" | "error";
        probe: MediaProbe;
        data: Uint8Array<ArrayBuffer>;
        message: string;
      }>,
    ) => {
      if (stopped) return;
      const message = event.data;
      if (message.type === "error") {
        fail(new Error(message.message));
        return;
      }
      if (message.type === "probe") {
        if (allowNative && nativeAudioSupported(message.probe)) {
          cleanup();
          resolve(file);
          return;
        }
        duration = Number(message.probe.format?.duration);
        // An unknown timeline cannot provide a useful underrun estimate.
        if (!Number.isFinite(duration) || duration <= 0) {
          cleanup();
          resolve(null);
          return;
        }
        const hasAudio = message.probe.streams?.some(
          (stream) => stream.codec_type === "audio",
        );
        media.addEventListener(
          "sourceopen",
          () => {
            try {
              buffer = media.addSourceBuffer(
                `video/mp4; codecs="${videoCodec}${hasAudio ? ",mp4a.40.2" : ""}"`,
              );
              buffer.addEventListener(
                "updateend",
                () => {
                  check();
                  pump();
                },
                { signal: listeners.signal },
              );
              buffer.addEventListener(
                "error",
                () => fail(new Error("Could not buffer converted video.")),
                { signal: listeners.signal },
              );
              media.duration = duration;
              worker.postMessage({ type: "convert" });
            } catch (error) {
              fail(error);
            }
          },
          { once: true, signal: listeners.signal },
        );
        url = URL.createObjectURL(media);
        video.loop = false;
        video.src = url;
      } else if (message.type === "encoding") {
        encodingStart = performance.now();
        status("Buffering converted video and audio...");
      } else if (message.type === "data") {
        parts.push(message.data);
        queue.push(message.data);
        pump();
      } else if (message.type === "done") {
        worker.terminate();
        finished = true;
        pump();
      }
    };
    status("Loading video converter...");
    worker.postMessage({ type: "load", file, coreURL, wasmURL });
  });
}
