import { useState, useEffect, useCallback } from "react";
import {
  getMirrorModule,
  initMirrorWasm,
  isWasmReady,
} from "@ascii-chat/shared/wasm";
// @ts-expect-error - Generated file without types
import MirrorModuleFactory from "../wasm/dist/mirror.js";
import { SITES } from "@ascii-chat/shared/utils";
import {
  BinarySettings,
  AsciiRenderer,
  PageLayout,
  ModeHeader,
  AsciiChatWebHead,
  VideoUploadModal,
} from "../components";
import type { BinarySettingsConfig } from "../components";
import { AsciiChatMode } from "../utils";
import {
  applyMirrorWasmSettings,
  useClientLike,
  useMirrorRenderLoop,
  useMirrorWebcam,
  setMirrorWasmDimensions,
} from "../hooks";
import { useTestPattern } from "@ascii-chat/shared/hooks";

export function MirrorPage() {
  const testPattern = useTestPattern();
  const [showUploadModal, setShowUploadModal] = useState(false);
  const [wasmModule, setWasmModule] = useState(() => {
    return getMirrorModule();
  });

  // Memoize WASM callbacks to prevent infinite re-render loops.
  // These are module-level functions that never change, so empty deps are correct.
  const initWasm = useCallback(() => {
    const t0 = performance.now();
    console.log(`[Mirror] initWasm start at ${t0.toFixed(1)}ms`);
    const promise = initMirrorWasm(MirrorModuleFactory);
    promise
      .then(() => {
        const t1 = performance.now();
        console.log(
          `[Mirror] initWasm complete at ${t1.toFixed(1)}ms (took ${(t1 - t0).toFixed(1)}ms)`,
        );
      })
      .catch((err) => {
        console.error(
          `[Mirror] initWasm error at ${performance.now().toFixed(1)}ms:`,
          err,
        );
      });
    return promise;
  }, []);

  const applyWasmSettings = useCallback((settings: BinarySettingsConfig) => {
    applyMirrorWasmSettings(settings);
  }, []);

  const setWasmDimensions = useCallback((cols: number, rows: number) => {
    setMirrorWasmDimensions(cols, rows);
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

  // Update wasmModule state immediately when WASM initialization completes
  useEffect(() => {
    const effectTime = performance.now();
    console.log(
      `[Mirror] wasmModule sync effect at ${effectTime.toFixed(0)}ms: wasmInitialized=${wasmInitialized}, wasmModule=${!!wasmModule}`,
    );
    if (wasmInitialized && !wasmModule) {
      const getModuleTime = performance.now();
      const module = getMirrorModule();
      const gotModuleTime = performance.now();
      console.log(
        `[Mirror] getMirrorModule at ${getModuleTime.toFixed(0)}ms returned ${!!module} (took ${(gotModuleTime - getModuleTime).toFixed(1)}ms)`,
      );
      if (module) {
        const setModuleTime = performance.now();
        console.log(
          `[Mirror] About to setWasmModule at ${setModuleTime.toFixed(0)}ms`,
        );
        setWasmModule(module);
        console.log(
          `[Mirror] setWasmModule enqueued at ${performance.now().toFixed(0)}ms`,
        );
      }
    }
  }, [wasmInitialized, wasmModule]);

  // Handle settings change
  const handleSettingsChange = (newSettings: BinarySettingsConfig) => {
    setSettings(newSettings);
    // Apply WASM settings immediately so renderer recreates if needed
    applyWasmSettings(newSettings);
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
    streamRef,
    animation: settings.animation ?? "matrix",
    animationEnabled: settings.animationEnabled ?? false,
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
        // Reapply all settings after resize to keep C side in sync with JS cache
        applyWasmSettings(settings);
      } catch (err) {
        console.error("Failed to sync dimensions to WASM:", err);
      }
    }
  }, [
    terminalDimensions,
    wasmInitialized,
    optionsManager,
    settings,
    applyWasmSettings,
  ]);

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
        header={
          <ModeHeader
            showSettings={showSettings}
            settingsPanel={
              <BinarySettings
                config={settings}
                onChange={handleSettingsChange}
                mode={AsciiChatMode.MIRROR}
              />
            }
            controlBar={{
              title: testPattern.enabled
                ? "Mirror mode (synthetic test input)"
                : "Mirror mode",
              dimensions: terminalDimensions,
              fps,
              targetFps: settings.targetFps,
              isWebcamRunning,
              mediaSource,
              onStartWebcam: startWebcam,
              onStopWebcam: stopWebcam,
              onUploadClick: () => setShowUploadModal(true),
              videoRef,
              onSettingsClick: () => setShowSettings(!showSettings),
              showConnectionButton: false,
              showSettingsButton: true,
              settingsOpen: showSettings,
            }}
          />
        }
        renderer={(() => {
          return (
            <AsciiRenderer
              ref={rendererRef}
              onDimensionsChange={handleDimensionsChange}
              onFpsChange={setFps}
              error={error}
              showFps={isWebcamRunning}
              wasmModuleReady={!!wasmModule}
              matrixMode={settings.matrixRain ?? false}
            />
          );
        })()}
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
