import { useState, useEffect, useCallback } from "react";
import {
  initMirrorWasm,
  isWasmReady,
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
  getMirrorModule,
} from "@ascii-chat/shared/wasm";
// @ts-expect-error - Generated file without types
import MirrorModuleFactory from "../wasm/dist/mirror.js";
import { SITES } from "@ascii-chat/shared/utils";
import {
  Settings,
  AsciiRenderer,
  PageControlBar,
  PageLayout,
  AsciiChatWebHead,
  VideoUploadModal,
} from "../components";
import type { SettingsConfig } from "../components";
import {
  AsciiChatMode,
  mapColorModeToWasm,
  mapColorFilterToWasm,
} from "../utils";
import {
  createWasmOptionsManager,
  useClientLike,
  useMirrorRenderLoop,
  useMirrorWebcam,
} from "../hooks";

export function MirrorPage() {
  const [showUploadModal, setShowUploadModal] = useState(false);

  // Memoize WASM callbacks to prevent infinite re-render loops.
  // These are module-level functions that never change, so empty deps are correct.
  const initWasm = useCallback(() => initMirrorWasm(MirrorModuleFactory), []);

  const applyWasmSettings = useCallback((settings: SettingsConfig) => {
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
  }, []);

  const setWasmDimensions = useCallback((cols: number, rows: number) => {
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
  }, []);

  const optionsManager = useClientLike({
    initWasm,
    isWasmReady,
    applyWasmSettings,
    setWasmDimensions,
  });

  const {
    videoRef,
    canvasRef,
    rendererRef,
    streamRef,
    objectUrlRef,
    lastFrameTimeRef,
    frameIntervalRef,
    isWebcamRunning,
    setIsWebcamRunning,
    mediaSource,
    setMediaSource,
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

  // Debug: log whenever terminalDimensions changes
  useEffect(() => {
    console.log(
      `[Mirror] terminalDimensions changed to ${terminalDimensions.cols}x${terminalDimensions.rows} at ${performance.now().toFixed(0)}ms`,
    );
  }, [terminalDimensions.cols, terminalDimensions.rows]);

  // Handle settings change
  const handleSettingsChange = (newSettings: SettingsConfig) => {
    setSettings(newSettings);
  };

  // Render loop that captures and converts frames to ASCII
  useMirrorRenderLoop({
    isWebcamRunning,
    terminalDimensions,
    captureFrame,
    canvasRef,
    rendererRef,
    debugCountRef,
    firstFrameTimeRef,
    frameIntervalRef,
  });

  // Webcam start logic and auto-start effects
  const { startWebcam, startVideoFile } = useMirrorWebcam({
    settings,
    videoRef,
    canvasRef,
    streamRef,
    objectUrlRef,
    lastFrameTimeRef,
    setIsWebcamRunning,
    setMediaSource,
    setError,
    wasmInitialized,
    isWebcamRunning,
    terminalDimensions,
  });

  const handleVideoFileSelect = useCallback(
    (file: File) => {
      void startVideoFile(file);
    },
    [startVideoFile],
  );

  // Sync terminal dimensions to WASM module when they change
  useEffect(() => {
    if (
      wasmInitialized &&
      terminalDimensions.cols > 0 &&
      terminalDimensions.rows > 0
    ) {
      try {
        optionsManager.setWasmDimensions(
          terminalDimensions.cols,
          terminalDimensions.rows,
        );
      } catch (err) {
        console.error("Failed to sync dimensions to WASM:", err);
      }
    }
  }, [terminalDimensions, wasmInitialized, optionsManager]);

  // Cleanup on unmount
  useEffect(() => {
    return () => {
      stopWebcam();
    };
  }, [stopWebcam]);

  return (
    <>
      <AsciiChatWebHead
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
            mediaSource={mediaSource}
            onStartWebcam={startWebcam}
            onStopWebcam={stopWebcam}
            onUploadClick={() => setShowUploadModal(true)}
            videoRef={videoRef}
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
            wasmModule={getMirrorModule()}
          />
        }
        modal={
          <VideoUploadModal
            isOpen={showUploadModal}
            onClose={() => setShowUploadModal(false)}
            onFileSelect={handleVideoFileSelect}
          />
        }
      />
    </>
  );
}
