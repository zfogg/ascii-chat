import { useCallback, useEffect, useRef, useState, RefObject } from "react";
import { SettingsConfig } from "../components/Settings";
import { AsciiRendererHandle } from "../components/AsciiRenderer";
import { useCanvasCapture } from "./useCanvasCapture";
import { getDefaultSettings } from "../utils/defaultSettings";

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
  /** Detect macOS for default settings (default: auto-detect) */
  isMacOS?: boolean;
}

export interface UseClientLikeReturn {
  // Refs
  videoRef: RefObject<HTMLVideoElement | null>;
  canvasRef: RefObject<HTMLCanvasElement | null>;
  rendererRef: RefObject<AsciiRendererHandle | null>;
  streamRef: RefObject<MediaStream | null>;
  animationFrameRef: RefObject<number | null>;
  lastFrameTimeRef: RefObject<number>;
  frameIntervalRef: RefObject<number>;

  // State
  isWebcamRunning: boolean;
  setIsWebcamRunning: (running: boolean) => void;
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

  // Auto-detect macOS
  const isMacOS =
    options.isMacOS ?? /Mac|iPhone|iPad|iPod/.test(navigator.userAgent);

  // Refs
  const videoRef = useRef<HTMLVideoElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const rendererRef = useRef<AsciiRendererHandle>(null);
  const streamRef = useRef<MediaStream | null>(null);
  const animationFrameRef = useRef<number | null>(null);
  const lastFrameTimeRef = useRef<number>(0);
  const frameIntervalRef = useRef<number>(1000 / 60);

  // State
  const [isWebcamRunning, setIsWebcamRunning] = useState(false);
  const [error, setError] = useState<string>("");
  const [terminalDimensions, setTerminalDimensions] = useState({
    cols: 0,
    rows: 0,
  });
  const [fps, setFps] = useState<number | undefined>();
  const [wasmInitialized, setWasmInitialized] = useState(false);
  const [showSettings, setShowSettings] = useState(false);
  const [settings, setSettings] = useState<SettingsConfig>(
    getDefaultSettings(isMacOS),
  );

  // Debug refs
  const debugCountRef = useRef(0);
  const firstFrameTimeRef = useRef<number | null>(null);

  // Initialize WASM on mount
  useEffect(() => {
    initWasm()
      .then(() => setWasmInitialized(true))
      .catch((err) => {
        console.error("WASM init error:", err);
        setError(`Failed to load WASM module: ${err}`);
      });
  }, [initWasm]);

  // Apply WASM settings when they change
  useEffect(() => {
    if (wasmInitialized && isWasmReady()) {
      try {
        applyWasmSettings(settings);
      } catch (err) {
        console.error("Failed to apply WASM settings:", err);
      }
    }
  }, [wasmInitialized, isWasmReady, applyWasmSettings, settings]);

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
      setTerminalDimensions(dims);
      onDimensionsChange?.(dims);
    },
    [onDimensionsChange],
  );

  // Stop webcam
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
      videoRef.current.srcObject = null;
    }

    rendererRef.current?.clear();
    setIsWebcamRunning(false);
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

    // State
    isWebcamRunning,
    setIsWebcamRunning,
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
