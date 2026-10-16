import {
  useCallback,
  useEffect,
  useRef,
  useState,
  type RefObject,
} from "react";
import { useCanvasCapture } from "@ascii-chat/shared/hooks";
import { MediaSourceType } from "./useClientLike";
import type { MediaSource } from "./useClientLike";
import { createGifPlayer } from "../utils/gifPlayer";
import { playMedia } from "../utils/playMedia";

interface Options {
  videoRef: RefObject<HTMLVideoElement | null>;
  canvasRef: RefObject<HTMLCanvasElement | null>;
  stopWebcam: () => void;
  cancelStart: () => void;
  setIsWebcamRunning: (running: boolean) => void;
  setMediaSource: (source: MediaSource) => void;
  setError: (error: string) => void;
}

/** One owner for file URLs, pending loads, GIF timing, and audible video playback. */
export function useMirrorFile({
  videoRef,
  canvasRef,
  stopWebcam,
  cancelStart,
  setIsWebcamRunning,
  setMediaSource,
  setError,
}: Options) {
  const [fileName, setFileName] = useState("");
  const [kind, setKind] = useState<"video" | "gif" | "image" | null>(null);
  const [loading, setLoading] = useState(false);
  const [status, setStatus] = useState("");
  const [paused, setPaused] = useState(false);
  const [muted, setMuted] = useState(false);
  const [volume, setVolume] = useState(1);
  const owner = useRef<AbortController | null>(null);
  const release = useRef<(() => void) | null>(null);
  const picture = useRef<((elapsed: number) => CanvasImageSource) | null>(null);
  const clock = useRef({ start: 0, elapsed: 0, paused: false });
  const { captureFrame: capture } = useCanvasCapture(videoRef, canvasRef);

  const dispose = useCallback(() => {
    owner.current?.abort();
    owner.current = null;
    release.current?.();
    release.current = null;
    picture.current = null;
  }, []);
  useEffect(() => dispose, [dispose]);

  const stop = useCallback(() => {
    dispose();
    cancelStart();
    stopWebcam();
    setLoading(false);
    setKind(null);
    setFileName("");
  }, [dispose, cancelStart, stopWebcam]);

  const start = useCallback(
    async (file: File) => {
      stop();
      const controller = new AbortController();
      owner.current = controller;
      const { signal } = controller;
      setError("");
      setLoading(true);
      setStatus("Opening media...");
      setFileName(file.name);
      const video = videoRef.current;
      const canvas = canvasRef.current;
      if (!video || !canvas) {
        setLoading(false);
        setError("Media renderer is not ready. Please try again.");
        return;
      }
      let url = URL.createObjectURL(file);
      const image = new Image();
      release.current = () => {
        image.removeAttribute("src");
        video.pause();
        video.srcObject = null;
        video.removeAttribute("src");
        video.load();
        URL.revokeObjectURL(url);
      };
      try {
        let width: number, height: number;
        const isGif = file.type === "image/gif" || /\.gif$/i.test(file.name);
        const isImage =
          file.type.startsWith("image/") ||
          /\.(png|jpe?g|webp|avif|bmp)$/i.test(file.name);
        if (isGif) {
          const data = await file.arrayBuffer();
          if (signal.aborted) return;
          const gif = createGifPlayer(data);
          width = gif.width;
          height = gif.height;
          picture.current = (elapsed) => gif.draw(elapsed);
        } else if (isImage) {
          image.src = url;
          await image.decode();
          if (signal.aborted) return;
          width = image.naturalWidth;
          height = image.naturalHeight;
          picture.current = () => image;
        } else {
          video.srcObject = null;
          video.loop = true;
          video.muted = false;
          video.volume = 1;
          video.playsInline = true;
          video.src = url;
          const { transcodeMedia } = await import("../utils/transcodeMedia");
          if (signal.aborted) return;
          const allowNative = /\.(mp4|m4v|webm)$/i.test(file.name);
          // Probe audio even when the browser accepts the video track. A
          // successful play() does not guarantee that audio was decoded.
          const prepared = await transcodeMedia(
            file,
            signal,
            setStatus,
            allowNative,
          );
          if (signal.aborted) return;
          if (prepared !== file) {
            URL.revokeObjectURL(url);
            url = URL.createObjectURL(prepared);
            video.src = url;
          }
          try {
            await playMedia(video, signal);
          } catch (error) {
            if (signal.aborted) return;
            if (
              prepared !== file ||
              !(error instanceof DOMException) ||
              error.name !== "NotSupportedError"
            )
              throw error;
            const converted = await transcodeMedia(file, signal, setStatus);
            if (signal.aborted) return;
            URL.revokeObjectURL(url);
            url = URL.createObjectURL(converted);
            video.src = url;
            await playMedia(video, signal);
          }
          if (signal.aborted) return;
          width = video.videoWidth;
          height = video.videoHeight;
          if (!width || !height)
            throw new Error("This file has no decodable video track.");
          video.addEventListener(
            "error",
            () => {
              if (!signal.aborted) {
                stop();
                setError(
                  "Video decoding failed. Try MP4 (H.264/AAC) or WebM (VP8/VP9/Opus).",
                );
              }
            },
            { signal },
          );
        }
        // Bound pixel transfer to WASM while preserving the source aspect ratio.
        const scale = Math.min(1, 1280 / Math.max(width, height));
        canvas.width = Math.max(1, Math.round(width * scale));
        canvas.height = Math.max(1, Math.round(height * scale));
        clock.current = { start: performance.now(), elapsed: 0, paused: false };
        setPaused(!picture.current && video.paused);
        setMuted(false);
        setVolume(1);
        setKind(isGif ? "gif" : isImage ? "image" : "video");
        setMediaSource(MediaSourceType.FILE);
        setIsWebcamRunning(true);
      } catch (error) {
        if (signal.aborted) return;
        stop();
        setError(
          `Cannot play "${file.name}". ${String(error)} Try a browser-supported video such as MP4 (H.264/AAC) or WebM, or a GIF, PNG, or JPEG image.`,
        );
      } finally {
        if (!signal.aborted) setLoading(false);
      }
    },
    [stop, videoRef, canvasRef, setError, setMediaSource, setIsWebcamRunning],
  );

  const captureFrame = useCallback(() => {
    if (!picture.current) return capture();
    const canvas = canvasRef.current;
    const ctx = canvas?.getContext("2d", { willReadFrequently: true });
    if (!canvas || !ctx) return null;
    const timing = clock.current;
    const elapsed =
      timing.elapsed + (timing.paused ? 0 : performance.now() - timing.start);
    ctx.fillStyle = "black";
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    ctx.drawImage(picture.current(elapsed), 0, 0, canvas.width, canvas.height);
    return capture(false);
  }, [canvasRef, capture]);

  const togglePause = useCallback(() => {
    if (picture.current) {
      const timing = clock.current;
      if (!timing.paused) timing.elapsed += performance.now() - timing.start;
      timing.start = performance.now();
      timing.paused = !timing.paused;
      setPaused(timing.paused);
    } else if (videoRef.current) {
      if (videoRef.current.paused)
        void videoRef.current
          .play()
          .catch((error: unknown) => setError(String(error)));
      else videoRef.current.pause();
    }
  }, [videoRef, setError]);

  const restart = useCallback(() => {
    clock.current = { start: performance.now(), elapsed: 0, paused: false };
    setPaused(false);
    if (!picture.current && videoRef.current) {
      videoRef.current.currentTime = 0;
      void videoRef.current
        .play()
        .catch((error: unknown) => setError(String(error)));
    }
  }, [videoRef, setError]);

  useEffect(() => {
    const video = videoRef.current;
    if (!video) return;
    const sync = () => {
      if (picture.current) return;
      setPaused(video.paused);
      setMuted(video.muted);
      setVolume(video.volume);
    };
    const events = ["play", "pause", "volumechange"];
    events.forEach((event) => video.addEventListener(event, sync));
    return () =>
      events.forEach((event) => video.removeEventListener(event, sync));
  }, [videoRef]);

  return {
    start,
    stop,
    captureFrame,
    fileName,
    kind,
    loading,
    status,
    paused,
    muted,
    volume,
    togglePause,
    restart,
  };
}
