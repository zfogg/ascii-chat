import { useEffect, useCallback, useRef, useState } from "react";
import "xterm/css/xterm.css";
import {
  initMirrorWasm,
  convertFrameToAscii,
  isWasmReady,
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
import { SITES } from "@ascii-chat/shared/utils";
import { Settings, SettingsConfig } from "../components/Settings";
import { AsciiRenderer } from "../components/AsciiRenderer";
import { PageControlBar } from "../components/PageControlBar";
import { PageLayout } from "../components/PageLayout";
import { WebClientHead } from "../components/WebClientHead";
import { AsciiChatMode } from "../utils/optionsHelp";
import {
  mapColorModeToWasm,
  mapColorFilterToWasm,
} from "../utils/colorMappers";
import { createWasmOptionsManager } from "../hooks/useWasmOptions";
import { useClientLike } from "../hooks/useClientLike";

export function MirrorPage() {
  // Create options manager
  const optionsManager = useClientLike({
    initWasm: () => initMirrorWasm({}),
    isWasmReady,
    applyWasmSettings: (settings) => {
      const om = createWasmOptionsManager(
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
      om?.applySettings(settings);
    },
    setWasmDimensions: (cols, rows) => {
      const om = createWasmOptionsManager(
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
      om?.setDimensions(cols, rows);
    },
  });

  const {
    videoRef,
    canvasRef,
    rendererRef,
    streamRef,
    lastFrameTimeRef,
    frameIntervalRef,
    isWebcamRunning,
    setIsWebcamRunning,
    error,
    setError,
    terminalDimensions,
    fps,
    setFps,
    wasmInitialized,
    showSettings,
    setShowSettings,
    settings,
    setSettings,
    captureFrame,
    handleDimensionsChange,
    stopWebcam,
    debugCountRef,
    firstFrameTimeRef,
  } = optionsManager;

  const devAutoStartRef = useRef(false);
  const [permissionGranted, setPermissionGranted] = useState(false);

  // Handle settings change
  const handleSettingsChange = (newSettings: SettingsConfig) => {
    setSettings(newSettings);
  };

  // Render loop that captures and converts frames to ASCII
  useEffect(() => {
    if (!isWebcamRunning) return;

    const renderFrame = () => {
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

      const asciiArt = convertFrameToAscii(
        frame.data,
        frame.width,
        frame.height,
      );
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

      rendererRef.current!.writeFrame(asciiArt);

      if (debugCountRef.current === 0) {
        console.timeEnd("[Mirror] Time to first frame render");
      }

      debugCountRef.current++;
    };

    const interval = setInterval(() => {
      renderFrame();
    }, frameIntervalRef.current);

    return () => clearInterval(interval);
    // oxlint-disable-next-line react-hooks/exhaustive-deps
  }, [isWebcamRunning, captureFrame]);

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
        const isMacOS = /Mac|iPhone|iPad|iPod/.test(navigator.userAgent);
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
      setIsWebcamRunning(true);
      console.timeEnd("[Mirror] Total startWebcam time");
    } catch (err) {
      setError(`Failed to start webcam: ${err}`);
    }
  }, [
    settings,
    videoRef,
    canvasRef,
    streamRef,
    lastFrameTimeRef,
    setIsWebcamRunning,
    setError,
  ]);

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
      !isWebcamRunning &&
      !devAutoStartRef.current
    ) {
      devAutoStartRef.current = true;
      console.log("[Mirror] Auto-starting webcam in development mode");
      Promise.resolve().then(() => startWebcam());
    }
  }, [wasmInitialized, permissionGranted, isWebcamRunning, startWebcam]);

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
        url={`${SITES.WEB}/mirror`}
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
            isWebcamRunning={isWebcamRunning}
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
            showFps={isWebcamRunning}
          />
        }
      />
    </>
  );
}
