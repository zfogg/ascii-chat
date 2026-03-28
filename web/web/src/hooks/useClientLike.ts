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
      `[useClientLike-Init] Starting WASM initialization at ${startTime.toFixed(0)}ms`,
      new Date().toISOString(),
    );
    initWasm()
      .then(() => {
        const endTime = performance.now();
        console.log(
          `[useClientLike-Init] WASM initialization complete at ${endTime.toFixed(0)}ms (took ${(endTime - startTime).toFixed(2)}ms)`,
        );

        console.log(
          `[useClientLike-Init] About to call setWasmInitialized(true) at ${performance.now().toFixed(0)}ms`,
        );
        setWasmInitialized(true);
        console.log(
          `[useClientLike-Init] setWasmInitialized(true) enqueued at ${performance.now().toFixed(0)}ms`,
        );
      })
      .catch((err) => {
        console.error("WASM init error:", err);
        setError(`Failed to load WASM module: ${err}`);
      });
  }, [initWasm]);

  // Track dependency changes to catch infinite loops
  useEffect(() => {
    console.log(
      `[useClientLike-Deps] wasmInitialized changed to ${wasmInitialized} at ${performance.now().toFixed(0)}ms`,
    );
  }, [wasmInitialized]);

  useEffect(() => {
    console.log(
      `[useClientLike-Deps] applyWasmSettings changed at ${performance.now().toFixed(0)}ms`,
    );
  }, [applyWasmSettings]);

  useEffect(() => {
    console.log(
      `[useClientLike-Deps] settings changed at ${performance.now().toFixed(0)}ms`,
    );
  }, [settings]);

  // Apply WASM settings immediately after init completes
  useEffect(() => {
    const effectTime = performance.now();
    console.log(
      `[useClientLike-Settings] *** SETTINGS EFFECT ENTERED at ${effectTime.toFixed(0)}ms: wasmInitialized=${wasmInitialized} ***`,
    );

    if (!wasmInitialized) {
      console.log(
        `[useClientLike-Settings] Skipping: wasmInitialized=false at ${performance.now().toFixed(0)}ms`,
      );
      return;
    }

    console.log(
      `[useClientLike-Settings] Proceeding with settings at ${performance.now().toFixed(0)}ms`,
    );

    // Retry up to 5 times if WASM isn't ready yet (typically ready immediately)
    let retries = 0;
    const tryApply = () => {
      const tryTime = performance.now();
      const ready = isWasmReady();
      console.log(
        `[useClientLike-Settings-Try] ATTEMPT at ${tryTime.toFixed(0)}ms: retries=${retries}, isWasmReady()=${ready}`,
      );

      if (!ready) {
        if (retries < 5) {
          retries++;
          console.log(
            `[useClientLike-Settings-Try] WASM not ready, scheduling retry ${retries}/5 at ${performance.now().toFixed(0)}ms`,
          );
          setTimeout(tryApply, 10); // 10ms retry
          return;
        }
        console.warn(
          `[useClientLike-Settings-Try] WASM not ready after 5 retries at ${performance.now().toFixed(0)}ms, giving up`,
        );
        return;
      }

      try {
        const applyStart = performance.now();
        console.log(
          `[useClientLike-Settings-Try] CALLING applyWasmSettings at ${applyStart.toFixed(0)}ms`,
        );
        applyWasmSettings(settings);
        const applyEnd = performance.now();
        console.log(
          `[useClientLike-Settings-Try] applyWasmSettings completed at ${applyEnd.toFixed(0)}ms (took ${(applyEnd - applyStart).toFixed(2)}ms)`,
        );
      } catch (err) {
        console.error(
          `[useClientLike-Settings-Try] applyWasmSettings failed: ${String(err)}`,
        );
      }
    };

    const tryApplyTime = performance.now();
    console.log(
      `[useClientLike-Settings] BEFORE first tryApply at ${tryApplyTime.toFixed(0)}ms`,
    );
    tryApply();
    console.log(
      `[useClientLike-Settings] AFTER first tryApply at ${performance.now().toFixed(0)}ms (elapsed ${(performance.now() - tryApplyTime).toFixed(2)}ms)`,
    );
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
