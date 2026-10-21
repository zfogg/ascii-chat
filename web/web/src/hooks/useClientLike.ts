import { useAutoGridSize } from "./useAutoGridSize";
import { useUrlState } from "./useUrlState";
import { useCallback, useEffect, useRef, useState, RefObject } from "react";
import type { BinarySettingsConfig, AsciiRendererHandle } from "../components";
import { useCanvasCapture } from "@ascii-chat/shared/hooks";
import { useRenderSettings } from "./useRenderSettings";

/**
 * Shared hook for client-like modes (Mirror, Client)
 * Provides all common state, refs, and effects needed for video capture + ASCII rendering
 */
export interface UseClientLikeOptions {
  /** Initialize WASM module */
  initWasm: () => Promise<void>;
  /** Check if WASM is ready */
  isWasmReady: () => boolean;
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
  settings: BinarySettingsConfig;
  setSettings: (settings: BinarySettingsConfig) => void;
  receiveSettings: (settings: BinarySettingsConfig) => void;

  // Hooks
  captureFrame: (drawVideo?: boolean) => {
    data: Uint8Array;
    width: number;
    height: number;
  } | null;

  // Handlers
  handleDimensionsChange: (dims: { cols: number; rows: number }) => void;
  stopWebcam: (preserveFrame?: boolean) => void;
  setWasmDimensions: (cols: number, rows: number) => void;

  // Debug refs
  debugCountRef: RefObject<number>;
  firstFrameTimeRef: RefObject<number | null>;
}

export function useClientLike(
  options: UseClientLikeOptions,
): UseClientLikeReturn {
  const { initWasm, isWasmReady, setWasmDimensions, onDimensionsChange } =
    options;

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
  const [fps, setFps] = useState<number | undefined>(0);
  const [wasmInitialized, setWasmInitialized] = useState(false);
  const [showSettings, setShowSettings] = useUrlState("settings", false);
  const [settings, setSettings, receiveSettings] = useRenderSettings();
  useAutoGridSize(setSettings);

  // Debug refs
  const debugCountRef = useRef(0);
  const firstFrameTimeRef = useRef<number | null>(null);

  // Initialize WASM on mount
  useEffect(() => {
    initWasm()
      .then(() => {
        setWasmInitialized(true);
      })
      .catch((err) => {
        console.error("WASM init error:", err);
        setError(`Failed to load WASM module: ${err}`);
      });
  }, [initWasm]);

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
  const { captureFrame } = useCanvasCapture(
    videoRef,
    canvasRef,
    false,
    mediaSource === MediaSourceType.SCREEN
      ? { faceFollow: false, faceFollowDebug: false }
      : settings,
  );

  // Handle dimension changes from AsciiRenderer
  const handleDimensionsChange = useCallback(
    (dims: { cols: number; rows: number }) => {
      setTerminalDimensions(dims);
      onDimensionsChange?.(dims);
    },
    [onDimensionsChange],
  );

  // Stop webcam or video file playback
  const stopWebcam = useCallback((preserveFrame = false) => {
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
      videoRef.current.removeAttribute("src");
      videoRef.current.load();
    }

    if (objectUrlRef.current) {
      URL.revokeObjectURL(objectUrlRef.current);
      objectUrlRef.current = null;
    }

    if (!preserveFrame) rendererRef.current?.clear();
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
    receiveSettings,

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
