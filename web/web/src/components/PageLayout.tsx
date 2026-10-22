import { applyAudioOutputDevice } from "@ascii-chat/shared/devices/mediaDevicePreferences";
import React from "react";
import { SessionContent } from "./SessionContent";
import {
  getMediaDevicePreferences,
  MEDIA_DEVICE_PREFERENCES_CHANGED,
} from "../utils/mediaDevicePreferences";

interface PageLayoutProps {
  videoRef: React.RefObject<HTMLVideoElement | null>;
  canvasRef: React.RefObject<HTMLCanvasElement | null>;
  header: React.ReactNode;
  renderer: React.ReactNode;
  modal?: React.ReactNode;
  loading?: boolean;
  loadingError?: string;
}

/** Keep capture elements mounted and reserve the renderer box during initialization. */
export function PageLayout({
  videoRef,
  canvasRef,
  header,
  renderer,
  modal,
  loading = false,
  loadingError,
}: PageLayoutProps) {
  React.useEffect(() => {
    const applySpeaker = () => {
      const speakerId = getMediaDevicePreferences().speakerId;
      const video = videoRef.current;
      if (!video) return;
      void applyAudioOutputDevice(video, speakerId).catch((error: unknown) => {
        console.warn(
          "Unable to route video audio to the selected speaker:",
          error,
        );
      });
    };

    applySpeaker();
    window.addEventListener(MEDIA_DEVICE_PREFERENCES_CHANGED, applySpeaker);
    return () =>
      window.removeEventListener(
        MEDIA_DEVICE_PREFERENCES_CHANGED,
        applySpeaker,
      );
  }, [videoRef]);

  return (
    <div className="flex-1 min-h-0 bg-terminal-bg text-terminal-fg flex flex-col pb-[25px] [&_.ascii-canvas-container]:[contain:size] [&_.ascii-canvas-container]:justify-end">
      {/* Hidden video and canvas for capture */}
      <div
        style={{
          position: "fixed",
          bottom: 0,
          right: 0,
          width: "1px",
          height: "1px",
          overflow: "hidden",
          pointerEvents: "none",
        }}
      >
        <video
          ref={videoRef}
          autoPlay
          muted
          playsInline
          style={{ width: "640px", height: "480px" }}
        />
        <canvas ref={canvasRef} />
      </div>

      {header}

      {/* ASCII output fills remaining space */}
      <SessionContent loading={loading} error={loadingError}>
        {renderer}
      </SessionContent>

      {/* Modals */}
      {modal}
    </div>
  );
}
