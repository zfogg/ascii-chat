import { useCallback, useEffect, useRef, useState, RefObject } from "react";
import type { SettingsConfig, AsciiRendererHandle } from "../components";
import { useCanvasCapture } from "@ascii-chat/shared/hooks";
import { getDefaultSettings } from "../utils";

/**
 * Shared hook for client-like modes (Mirror, Client)
 * Provides all common state, refs, and effects needed for video capture + ASCII rendering
 */
export interface UseClientLikeOptions {
  /** Initialize WASM module */
  initWasm: () => Promise<void>;
  /** Check if WASM is ready */
  isWasmReady: () => boolean;
  /** Apply settings to WASM */
  applyWasmSettings: (settings: SettingsConfig) => void;
  /** Set WASM terminal dimensions */
  setWasmDimensions: (cols: number, rows: number) => void;
  /** Optional callback after dimensions change (for network modes) */
  onDimensionsChange?: (dims: { cols: number; rows: number }) => void;
}

import { MediaSourceType, type MediaSource } from "@ascii-chat/shared/utils";
export { MediaSourceType, type MediaSource };

export interface UseClientLikeReturn {
  // Refs
  videoRef: RefObject<HTMLVideoElement | null>;
  canvasRef: RefObject<HTMLCanvasElement | null>;
  rendererRef: RefObject<AsciiRendererHandle | null>;
  streamRef: RefObject<MediaStream | null>;
  animationFrameRef: RefObject<number | null>;
  lastFrameTimeRef: RefObject<number>;
  frameIntervalRef: RefObject<number>;
  objectUrlRef: RefObject<string | null>;

  // State
  isWebcamRunning: boolean;
  setIsWebcamRunning: (running: boolean) => void;
  mediaSource: MediaSource;
  setMediaSource: (source: MediaSource) => void;
  error: string;
  setError: (error: string) => void;
  terminalDimensions: { cols: number; rows: number };
  setTerminalDimensions: (dims: { cols: number; rows: number }) => void;
  fps: number | undefined;
  setFps: (fps: number | undefined) => void;
  wasmInitialized: boolean;
  showSettings: boolean;
  setShowSettings: (show: boolean) => void;
  settings: SettingsConfig;
  setSettings: (settings: SettingsConfig) => void;

  // Hooks
  captureFrame: () => {
    data: Uint8Array;
    width: number;
    height: number;
  } | null;

  // Handlers
  handleDimensionsChange: (dims: { cols: number; rows: number }) => void;
  stopWebcam: () => void;
  setWasmDimensions: (cols: number, rows: number) => void;

  // Debug refs
  debugCountRef: RefObject<number>;
  firstFrameTimeRef: RefObject<number | null>;
}

export function useClientLike(
  options: UseClientLikeOptions,
): UseClientLikeReturn {
  const {
    initWasm,
    isWasmReady,
    applyWasmSettings,
    setWasmDimensions,
    onDimensionsChange,
  } = options;

  // Refs
  const videoRef = useRef<HTMLVideoElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const rendererRef = useRef<AsciiRendererHandle>(null);
  const streamRef = useRef<MediaStream | null>(null);
  const animationFrameRef = useRef<number | null>(null);
  const lastFrameTimeRef = useRef<number>(0);
  const frameIntervalRef = useRef<number>(1000 / 60);
  const objectUrlRef = useRef<string | null>(null);

  // State
  const [isWebcamRunning, setIsWebcamRunning] = useState(false);
  const [mediaSource, setMediaSource] = useState<MediaSource>(null);
  const [error, setError] = useState<string>("");
  const [terminalDimensions, setTerminalDimensions] = useState({
    cols: 0,
    rows: 0,
  });
  const [fps, setFps] = useState<number | undefined>();
  const [wasmInitialized, setWasmInitialized] = useState(false);
  const [showSettings, setShowSettings] = useState(false);
  const [settings, setSettings] =
    useState<SettingsConfig>(getDefaultSettings());

  // Debug refs
  const debugCountRef = useRef(0);
  const firstFrameTimeRef = useRef<number | null>(null);

  // Initialize WASM on mount
  useEffect(() => {
    const startTime = performance.now();
    console.log(
      "[useClientLike] Starting WASM initialization at",
      new Date().toISOString(),
      `(perf: ${startTime.toFixed(2)}ms)`,
    );
    initWasm()
      .then(() => {
        const endTime = performance.now();
        console.log(
          "[useClientLike] WASM initialization complete, setting wasmInitialized=true at",
          new Date().toISOString(),
          `(perf: ${endTime.toFixed(2)}ms, took ${(endTime - startTime).toFixed(2)}ms)`,
        );

        // Log immediately after setting state
        console.log("[useClientLike] About to call setWasmInitialized(true)");
        setWasmInitialized(true);
        console.log("[useClientLike] setWasmInitialized(true) called");
      })
      .catch((err) => {
        console.error("WASM init error:", err);
        setError(`Failed to load WASM module: ${err}`);
      });
  }, [initWasm]);

  // Apply WASM settings immediately after init completes
  useEffect(() => {
    console.log(
      `[useClientLike] Settings effect triggered: wasmInitialized=${wasmInitialized}`,
    );

    if (!wasmInitialized) {
      console.log(
        "[useClientLike] Skipping settings: WASM not initialized yet",
      );
      return;
    }

    console.log(
      "[useClientLike] wasmInitialized=true, applying settings at",
      new Date().toISOString(),
    );

    // Retry up to 5 times if WASM isn't ready yet (typically ready immediately)
    let retries = 0;
    const tryApply = () => {
      console.log(
        `[useClientLike] tryApply called: isWasmReady()=${isWasmReady()}, retries=${retries}`,
      );

      if (!isWasmReady()) {
        if (retries < 5) {
          retries++;
          console.log(
            `[useClientLike] WASM not ready yet, retrying in 10ms (attempt ${retries}/5)`,
          );
          setTimeout(tryApply, 10); // 10ms retry
          return;
        }
        console.warn(
          "[useClientLike] WASM still not ready after 5 retries, skipping settings",
        );
        return;
      }

      try {
        console.log("[useClientLike] Calling applyWasmSettings...");
        const settingsStart = performance.now();
        applyWasmSettings(settings);
        const settingsTime = performance.now() - settingsStart;
        console.log(
          `[useClientLike] Settings applied successfully in ${settingsTime.toFixed(2)}ms`,
        );
      } catch (err) {
        console.error("Failed to apply WASM settings:", err);
      }
    };

    tryApply();
    // oxlint-disable-next-line react-hooks/exhaustive-deps
  }, [wasmInitialized, applyWasmSettings, settings]);

  // Update frame interval when target FPS changes
  useEffect(() => {
    frameIntervalRef.current = 1000 / settings.targetFps;
  }, [settings.targetFps]);

  // Update WASM terminal dimensions when terminal size changes
  useEffect(() => {
    if (
      isWasmReady() &&
      terminalDimensions.cols > 0 &&
      terminalDimensions.rows > 0
    ) {
      try {
        setWasmDimensions(terminalDimensions.cols, terminalDimensions.rows);
      } catch (err) {
        console.error("Failed to set terminal dimensions in WASM:", err);
      }
    }
  }, [terminalDimensions, isWasmReady, setWasmDimensions]);

  // Canvas capture hook
  const { captureFrame } = useCanvasCapture(videoRef, canvasRef);

  // Handle dimension changes from AsciiRenderer
  const handleDimensionsChange = useCallback(
    (dims: { cols: number; rows: number }) => {
      console.log(
        `[useClientLike] handleDimensionsChange called with ${dims.cols}x${dims.rows}`,
      );
      setTerminalDimensions(dims);
      onDimensionsChange?.(dims);
    },
    [onDimensionsChange],
  );

  // Stop webcam or video file playback
  const stopWebcam = useCallback(() => {
    if (animationFrameRef.current !== null) {
      cancelAnimationFrame(animationFrameRef.current);
      animationFrameRef.current = null;
    }

    if (streamRef.current) {
      streamRef.current.getTracks().forEach((track) => track.stop());
      streamRef.current = null;
    }

    if (videoRef.current) {
      videoRef.current.pause();
      videoRef.current.srcObject = null;
      videoRef.current.src = "";
    }

    if (objectUrlRef.current) {
      URL.revokeObjectURL(objectUrlRef.current);
      objectUrlRef.current = null;
    }

    rendererRef.current?.clear();
    setIsWebcamRunning(false);
    setMediaSource(null);
  }, []);

  return {
    // Refs
    videoRef,
    canvasRef,
    rendererRef,
    streamRef,
    animationFrameRef,
    lastFrameTimeRef,
    frameIntervalRef,
    objectUrlRef,

    // State
    isWebcamRunning,
    setIsWebcamRunning,
    mediaSource,
    setMediaSource,
    error,
    setError,
    terminalDimensions,
    setTerminalDimensions,
    fps,
    setFps,
    wasmInitialized,
    showSettings,
    setShowSettings,
    settings,
    setSettings,

    // Hooks
    captureFrame,

    // Handlers
    handleDimensionsChange,
    stopWebcam,
    setWasmDimensions,

    // Debug refs
    debugCountRef,
    firstFrameTimeRef,
  };
}
