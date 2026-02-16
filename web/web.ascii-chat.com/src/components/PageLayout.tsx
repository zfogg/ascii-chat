import React from "react";

interface PageLayoutProps {
  videoRef: React.RefObject<HTMLVideoElement | null>;
  canvasRef: React.RefObject<HTMLCanvasElement | null>;
  showSettings: boolean;
  settingsPanel?: React.ReactNode;
  controlBar: React.ReactNode;
  renderer: React.ReactNode;
  modal?: React.ReactNode;
}

export function PageLayout({
  videoRef,
  canvasRef,
  showSettings,
  settingsPanel,
  controlBar,
  renderer,
  modal,
}: PageLayoutProps) {
  return (
    <div className="flex-1 bg-terminal-bg text-terminal-fg flex flex-col">
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

      {/* Settings Panel */}
      {showSettings && settingsPanel}

      {/* Control bar */}
      {controlBar}

      {/* ASCII output fills remaining space */}
      {renderer}

      {/* Modals */}
      {modal}
    </div>
  );
}
