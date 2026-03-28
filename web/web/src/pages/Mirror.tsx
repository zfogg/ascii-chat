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
  const pageRenderTime = performance.now();
  console.log(`[Mirror] MirrorPage render at ${pageRenderTime.toFixed(0)}ms`);

  const [showUploadModal, setShowUploadModal] = useState(false);
  const [wasmModule, setWasmModule] = useState(() => {
    const moduleInitTime = performance.now();
    const module = getMirrorModule();
    console.log(
      `[Mirror] Initial useState for wasmModule: getMirrorModule() at ${moduleInitTime.toFixed(0)}ms returned ${!!module}`,
    );
    return module;
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

  const applyWasmSettings = useCallback((settings: SettingsConfig) => {
    const applyStart = performance.now();
    console.log(
      `[Mirror] applyWasmSettings called at ${applyStart.toFixed(0)}ms`,
    );

    const omStart = performance.now();
    console.log(
      `[Mirror] Creating WasmOptionsManager at ${omStart.toFixed(0)}ms`,
    );
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
    const omEnd = performance.now();
    console.log(
      `[Mirror] WasmOptionsManager created at ${omEnd.toFixed(0)}ms (took ${(omEnd - omStart).toFixed(1)}ms)`,
    );

    const beforeApply = performance.now();
    console.log(
      `[Mirror] Calling om.applySettings at ${beforeApply.toFixed(0)}ms`,
    );
    om?.applySettings(settings);
    const afterApply = performance.now();
    console.log(
      `[Mirror] om.applySettings returned at ${afterApply.toFixed(0)}ms (took ${(afterApply - beforeApply).toFixed(1)}ms)`,
    );
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

  const optionsManagerTime = performance.now();
  console.log(
    `[Mirror] Creating useClientLike at ${optionsManagerTime.toFixed(0)}ms`,
  );
  const optionsManager = useClientLike({
    initWasm,
    isWasmReady,
    applyWasmSettings,
    setWasmDimensions,
  });
  console.log(
    `[Mirror] useClientLike returned at ${performance.now().toFixed(0)}ms (${(performance.now() - optionsManagerTime).toFixed(1)}ms to return)`,
  );

  console.log(
    `[Mirror] optionsManager state: wasmInitialized=${optionsManager.wasmInitialized}, terminalDimensions=${optionsManager.terminalDimensions.cols}x${optionsManager.terminalDimensions.rows}`,
  );

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
  const handleSettingsChange = (newSettings: SettingsConfig) => {
    setSettings(newSettings);
  };

  // Render loop that captures and converts frames to ASCII
  const renderLoopTime = performance.now();
  console.log(
    `[Mirror] Calling useMirrorRenderLoop at ${renderLoopTime.toFixed(0)}ms`,
  );
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
  console.log(
    `[Mirror] useMirrorRenderLoop returned at ${performance.now().toFixed(0)}ms`,
  );

  // Webcam start logic and auto-start effects
  const webcamHookTime = performance.now();
  console.log(
    `[Mirror] Calling useMirrorWebcam at ${webcamHookTime.toFixed(0)}ms`,
  );
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

  console.log(
    `[Mirror] useMirrorWebcam returned at ${performance.now().toFixed(0)}ms`,
  );

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

  const returnTime = performance.now();
  console.log(
    `[Mirror] About to return JSX at ${returnTime.toFixed(0)}ms (total render time: ${(returnTime - pageRenderTime).toFixed(1)}ms)`,
  );

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
        renderer={(() => {
          console.log(
            `[Mirror] Rendering AsciiRenderer with wasmModuleReady=${!!wasmModule}`,
          );
          return (
            <AsciiRenderer
              ref={rendererRef}
              onDimensionsChange={handleDimensionsChange}
              onFpsChange={setFps}
              error={error}
              showFps={isWebcamRunning}
              wasmModuleReady={!!wasmModule}
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
