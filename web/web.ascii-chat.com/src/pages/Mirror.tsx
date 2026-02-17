import { useEffect, useRef, useState, useCallback, useMemo } from "react";
import "xterm/css/xterm.css";
import {
  initMirrorWasm,
  convertFrameToAscii,
  isWasmReady,
  ColorMode as WasmColorMode,
  ColorFilter as WasmColorFilter,
} from "../wasm/mirror";
import {
  setDimensions,
  getDimensions,
  setColorMode,
  getColorMode,
  setColorFilter,
  getColorFilter,
  setPalette,
  getPalette,
  setPaletteChars,
  getPaletteChars,
  setMatrixRain,
  getMatrixRain,
  setFlipX,
  getFlipX,
  setTargetFps,
  getTargetFps,
} from "../wasm/settings";
import {
  Settings,
  SettingsConfig,
  ColorMode,
  ColorFilter,
} from "../components/Settings";
import {
  AsciiRenderer,
  AsciiRendererHandle,
} from "../components/AsciiRenderer";
import { PageControlBar } from "../components/PageControlBar";
import { PageLayout } from "../components/PageLayout";
import { WebClientHead } from "../components/WebClientHead";
import { AsciiChatMode } from "../utils/optionsHelp";
import { useCanvasCapture } from "../hooks/useCanvasCapture";
import { createWasmOptionsManager } from "../hooks/useWasmOptions";
import { useRenderLoop } from "../hooks/useRenderLoop";

// Helper functions to map Settings types to WASM enums
function mapColorMode(mode: ColorMode): WasmColorMode {
  const mapping: Record<ColorMode, WasmColorMode> = {
    auto: WasmColorMode.AUTO,
    none: WasmColorMode.NONE,
    "16": WasmColorMode.COLOR_16,
    "256": WasmColorMode.COLOR_256,
    truecolor: WasmColorMode.TRUECOLOR,
  };
  return mapping[mode];
}

function mapColorFilter(filter: ColorFilter): WasmColorFilter {
  const mapping: Record<ColorFilter, WasmColorFilter> = {
    none: WasmColorFilter.NONE,
    black: WasmColorFilter.BLACK,
    white: WasmColorFilter.WHITE,
    green: WasmColorFilter.GREEN,
    magenta: WasmColorFilter.MAGENTA,
    fuchsia: WasmColorFilter.FUCHSIA,
    orange: WasmColorFilter.ORANGE,
    teal: WasmColorFilter.TEAL,
    cyan: WasmColorFilter.CYAN,
    pink: WasmColorFilter.PINK,
    red: WasmColorFilter.RED,
    yellow: WasmColorFilter.YELLOW,
    rainbow: WasmColorFilter.RAINBOW,
  };
  return mapping[filter];
}

export function MirrorPage() {
  const videoRef = useRef<HTMLVideoElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const rendererRef = useRef<AsciiRendererHandle>(null);
  const { captureFrame } = useCanvasCapture(videoRef, canvasRef);
  const [isRunning, setIsRunning] = useState(false);
  const [error, setError] = useState<string>("");
  const [terminalDimensions, setTerminalDimensions] = useState({
    cols: 0,
    rows: 0,
  });
  const [fps, setFps] = useState<number | undefined>();
  const streamRef = useRef<MediaStream | null>(null);
  const animationFrameRef = useRef<number | null>(null);
  const lastFrameTimeRef = useRef<number>(0);
  const frameIntervalRef = useRef<number>(1000 / 60);

  // Detect macOS/iOS for webcam flip default
  const isMacOS = /Mac|iPhone|iPad|iPod/.test(navigator.userAgent);

  // Settings state
  const [settings, setSettings] = useState<SettingsConfig>({
    width: 640,
    height: 480,
    targetFps: 60,
    colorMode: "truecolor",
    colorFilter: "none",
    palette: "standard",
    paletteChars: " =#░░▒▒▓▓██",
    matrixRain: false,
    flipX: isMacOS,
  });
  const [showSettings, setShowSettings] = useState(false);
  const [wasmInitialized, setWasmInitialized] = useState(false);

  const optionsManager = useMemo(() => {
    if (!wasmInitialized || !isWasmReady()) return null;

    return createWasmOptionsManager(
      setColorMode,
      getColorMode,
      setColorFilter,
      getColorFilter,
      setPalette,
      getPalette,
      setPaletteChars,
      getPaletteChars,
      setMatrixRain,
      getMatrixRain,
      setFlipX,
      getFlipX,
      setDimensions,
      getDimensions,
      setTargetFps,
      getTargetFps,
      mapColorMode,
      mapColorFilter,
    );
  }, [wasmInitialized]);

  // Handle settings change
  const handleSettingsChange = (newSettings: SettingsConfig) => {
    console.log("[Mirror] handleSettingsChange:", newSettings);
    setSettings(newSettings);
    frameIntervalRef.current = 1000 / newSettings.targetFps;

    if (optionsManager && isWasmReady()) {
      try {
        console.log("[Mirror] Calling optionsManager.applySettings");
        optionsManager.applySettings({
          ...newSettings,
          flipX: newSettings.flipX ?? isMacOS,
        });
        console.log("[Mirror] applySettings completed successfully");
      } catch (err) {
        console.error("Failed to apply WASM settings:", err);
      }
    } else {
      console.log(
        "[Mirror] Cannot apply settings - optionsManager or WASM not ready",
        { optionsManager: !!optionsManager, wasmReady: isWasmReady() },
      );
    }
  };

  // Handle dimension changes from AsciiRenderer (terminal display size, not webcam input)
  const handleDimensionsChange = (dims: { cols: number; rows: number }) => {
    setTerminalDimensions(dims);
  };

  // Apply initial settings to WASM after it initializes
  useEffect(() => {
    if (wasmInitialized && optionsManager && isWasmReady()) {
      try {
        optionsManager.applySettings(settings);
      } catch (err) {
        console.error("Failed to apply initial WASM settings:", err);
      }
    }
  }, [wasmInitialized, optionsManager, settings]);

  // Update WASM terminal dimensions when terminal size changes
  useEffect(() => {
    if (
      optionsManager &&
      isWasmReady() &&
      terminalDimensions.cols > 0 &&
      terminalDimensions.rows > 0
    ) {
      try {
        optionsManager.setDimensions(
          terminalDimensions.cols,
          terminalDimensions.rows,
        );
      } catch (err) {
        console.error("Failed to set terminal dimensions in WASM:", err);
      }
    }
  }, [terminalDimensions, optionsManager]);

  // Initialize WASM on mount
  useEffect(() => {
    initMirrorWasm({})
      .then(() => setWasmInitialized(true))
      .catch((err) => {
        console.error("WASM init error:", err);
        setError(`Failed to load WASM module: ${err}`);
      });
  }, []);

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
    setIsRunning(false);
  }, []);

  const renderFrame = useCallback(() => {
    if (!isWasmReady() || !rendererRef.current) return;

    const frame = captureFrame();
    if (!frame) return;

    const asciiArt = convertFrameToAscii(frame.data, frame.width, frame.height);
    rendererRef.current.writeFrame(asciiArt);
  }, [captureFrame]);

  const { startRenderLoop } = useRenderLoop(
    renderFrame,
    frameIntervalRef,
    lastFrameTimeRef,
    (err) => {
      setError(`Render error: ${err}`);
      stopWebcam();
    },
  );

  const startWebcam = useCallback(async () => {
    if (!videoRef.current || !canvasRef.current) {
      setError("Video or canvas element not ready");
      return;
    }

    const dims = rendererRef.current?.getDimensions();
    if (!dims || dims.cols === 0 || dims.rows === 0) {
      setError("Terminal not ready. Please wait a moment and try again.");
      return;
    }

    try {
      // Reapply settings to WASM before starting
      if (isWasmReady()) {
        setColorMode(mapColorMode(settings.colorMode));
        setColorFilter(mapColorFilter(settings.colorFilter));
        setPalette(settings.palette);
        if (settings.palette === "custom" && settings.paletteChars) {
          setPaletteChars(settings.paletteChars);
        }
        setMatrixRain(settings.matrixRain ?? false);
        setFlipX(settings.flipX ?? isMacOS);
      }

      const stream = await navigator.mediaDevices.getUserMedia({
        video: {
          width: { ideal: settings.width },
          height: { ideal: settings.height },
          facingMode: "user",
        },
        audio: false,
      });

      streamRef.current = stream;
      videoRef.current.srcObject = stream;

      await new Promise<void>((resolve) => {
        videoRef.current!.addEventListener(
          "loadedmetadata",
          () => {
            const video = videoRef.current!;
            const canvas = canvasRef.current!;
            canvas.width = video.videoWidth;
            canvas.height = video.videoHeight;
            resolve();
          },
          { once: true },
        );
      });

      lastFrameTimeRef.current = performance.now();
      setIsRunning(true);
      startRenderLoop();
    } catch (err) {
      setError(`Failed to start webcam: ${err}`);
    }
  }, [settings, isMacOS, startRenderLoop]);

  // Cleanup on unmount
  useEffect(() => {
    return () => {
      stopWebcam();
    };
  }, [stopWebcam]);

  return (
    <>
      <WebClientHead
        title="Mirror Mode - ascii-chat Web Client"
        description="Test your webcam with real-time ASCII art rendering. See yourself in terminal-style graphics."
        url="https://web.ascii-chat.com/mirror"
      />
      <PageLayout
        videoRef={videoRef}
        canvasRef={canvasRef}
        showSettings={showSettings}
        settingsPanel={
          <Settings
            config={settings}
            onChange={handleSettingsChange}
            mode={AsciiChatMode.MIRROR}
          />
        }
        controlBar={
          <PageControlBar
            title="ASCII Mirror"
            dimensions={terminalDimensions}
            fps={fps}
            targetFps={settings.targetFps}
            isWebcamRunning={isRunning}
            onStartWebcam={startWebcam}
            onStopWebcam={stopWebcam}
            onSettingsClick={() => setShowSettings(!showSettings)}
            showConnectionButton={false}
            showSettingsButton={true}
          />
        }
        renderer={
          <AsciiRenderer
            ref={rendererRef}
            onDimensionsChange={handleDimensionsChange}
            onFpsChange={setFps}
            error={error}
            showFps={isRunning}
          />
        }
      />
    </>
  );
}
