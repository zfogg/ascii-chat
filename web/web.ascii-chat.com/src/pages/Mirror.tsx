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
import {
  mapColorModeToWasm,
  mapColorFilterToWasm,
} from "../utils/colorMappers";
import { useCanvasCapture } from "../hooks/useCanvasCapture";
import { createWasmOptionsManager } from "../hooks/useWasmOptions";
import { useRenderLoop } from "../hooks/useRenderLoop";

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
  const [permissionGranted, setPermissionGranted] = useState(false);

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
      mapColorModeToWasm,
      mapColorFilterToWasm,
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

  const debugCountRef = useRef(0);
  const firstFrameTimeRef = useRef<number | null>(null);
  const devAutoStartRef = useRef(false);

  const renderFrame = useCallback(() => {
    if (!isWasmReady() || !rendererRef.current) return;

    const now = performance.now();
    if (firstFrameTimeRef.current === null) {
      firstFrameTimeRef.current = now;
      console.time("[Mirror] Time to first frame render");
    }

    const frame = captureFrame();
    if (!frame) {
      if (debugCountRef.current % 300 === 0) {
        console.log("[Mirror] captureFrame returned null");
      }
      return;
    }

    if (debugCountRef.current === 0) {
      console.log(
        `[Mirror] First frame captured at ${now - firstFrameTimeRef.current!}ms: ${frame.width}x${frame.height}, ${frame.data.length} bytes`,
      );
    }

    const asciiArt = convertFrameToAscii(frame.data, frame.width, frame.height);
    if (!asciiArt) {
      if (debugCountRef.current % 300 === 0) {
        console.log("[Mirror] convertFrameToAscii returned empty");
      }
      return;
    }

    if (debugCountRef.current === 0) {
      console.log(
        `[Mirror] First ASCII art generated at ${performance.now() - firstFrameTimeRef.current!}ms: ${asciiArt.length} chars`,
      );
    }

    rendererRef.current.writeFrame(asciiArt);

    if (debugCountRef.current === 0) {
      console.timeEnd("[Mirror] Time to first frame render");
    }

    debugCountRef.current++;
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
    const clickTime = performance.now();
    console.log("[Mirror] Button clicked");
    console.time("[Mirror] Total startWebcam time");

    if (!videoRef.current || !canvasRef.current) {
      setError("Video or canvas element not ready");
      return;
    }

    try {
      console.time("[Mirror] WASM settings");
      if (isWasmReady()) {
        setColorMode(mapColorModeToWasm(settings.colorMode));
        setColorFilter(mapColorFilterToWasm(settings.colorFilter));
        setPalette(settings.palette);
        if (settings.palette === "custom" && settings.paletteChars) {
          setPaletteChars(settings.paletteChars);
        }
        setMatrixRain(settings.matrixRain ?? false);
        setFlipX(settings.flipX ?? isMacOS);
      }
      console.timeEnd("[Mirror] WASM settings");

      console.log("[Mirror] Calling getUserMedia...");
      console.time("[Mirror] getUserMedia (incl browser permission)");
      const stream = await navigator.mediaDevices.getUserMedia({
        video: {
          width: { ideal: settings.width },
          height: { ideal: settings.height },
          facingMode: "user",
        },
        audio: false,
      });
      console.timeEnd("[Mirror] getUserMedia (incl browser permission)");
      console.log(
        `[Mirror] Stream received after ${performance.now() - clickTime}ms from button click`,
      );

      streamRef.current = stream;
      videoRef.current.srcObject = stream;

      console.time("[Mirror] loadedmetadata");
      await new Promise<void>((resolve) => {
        videoRef.current!.addEventListener(
          "loadedmetadata",
          () => {
            console.timeEnd("[Mirror] loadedmetadata");
            const canvas = canvasRef.current!;
            // Use requested dimensions from settings, not video.videoWidth
            // (which are 2x2 when loadedmetadata fires, before actual stream loads)
            canvas.width = settings.width;
            canvas.height = settings.height;
            console.log(
              `[Mirror] canvas set to ${canvas.width}x${canvas.height} (from settings)`,
            );
            resolve();
          },
          { once: true },
        );
      });

      lastFrameTimeRef.current = performance.now();
      setIsRunning(true);
      startRenderLoop();
      console.timeEnd("[Mirror] Total startWebcam time");
    } catch (err) {
      setError(`Failed to start webcam: ${err}`);
    }
  }, [settings, isMacOS, startRenderLoop]);

  // Request camera permission early on page load (browsers require explicit permission)
  // This helps Firefox and other browsers allow camera access before auto-start
  useEffect(() => {
    const requestPermission = async () => {
      try {
        console.log("[Mirror] Requesting camera permission on page load");
        // Request minimal 1x1 stream just to trigger permission dialog
        const stream = await navigator.mediaDevices.getUserMedia({
          video: { width: { ideal: 1 }, height: { ideal: 1 } },
          audio: false,
        });
        console.log("[Mirror] Permission granted, stopping stream");
        stream.getTracks().forEach((track) => track.stop());
        // Add a small delay to ensure Firefox properly releases the camera
        // before the auto-start tries to access it
        await new Promise((resolve) => setTimeout(resolve, 100));
        setPermissionGranted(true);
      } catch (err) {
        const message = err instanceof Error ? err.message : String(err);
        console.log(
          "[Mirror] Permission request failed (user may have denied):",
          message,
        );
        // Even if permission request fails, proceed with auto-start
        // (user may deny and then manually grant, or have already granted)
        setPermissionGranted(true);
      }
    };
    requestPermission();
  }, []);

  // Auto-start webcam in development mode
  useEffect(() => {
    if (
      import.meta.env["NODE_ENV"] !== "production" &&
      wasmInitialized &&
      permissionGranted &&
      !isRunning &&
      !devAutoStartRef.current
    ) {
      devAutoStartRef.current = true;
      console.log("[Mirror] Auto-starting webcam in development mode");
      Promise.resolve().then(() => startWebcam());
    }
  }, [wasmInitialized, permissionGranted, isRunning, startWebcam]);

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
