import { createRoot } from "react-dom/client";
import { useEffect, useState } from "react";
import { BrowserRouter } from "react-router-dom";
import { HeadingProvider } from "@ascii-chat/shared/components";
import { MirrorDemoWidget } from "./components/demo";
import "./styles/index.css";

const candidate = { title: "OK Go — White Knuckles", id: "nHlJODYBLKs" };
const src = "/assets/demo-video-white-knuckles.webm";
document.title = `${candidate.title} | ASCII demo preview`;

export function PlaybackStatus() {
  const [status, setStatus] = useState("Ready");
  const [seek, setSeek] = useState("0");
  useEffect(() => {
    const video = document.querySelector<HTMLVideoElement>("video");
    if (!video) return;
    let context: AudioContext | undefined;
    let analyser: AnalyserNode | undefined;
    const connect = () => {
      if (!context) {
        context = new AudioContext();
        analyser = context.createAnalyser();
        context.createMediaElementSource(video).connect(analyser);
        analyser.connect(context.destination);
      }
      void context.resume();
    };
    video.addEventListener("playing", connect);
    const timer = setInterval(() => {
      if (!video.currentSrc) return;
      const samples = new Float32Array(analyser?.fftSize ?? 2048);
      analyser?.getFloatTimeDomainData(samples);
      const rms = Math.sqrt(
        samples.reduce((sum, value) => sum + value * value, 0) / samples.length,
      );
      const quality = video.getVideoPlaybackQuality();
      setStatus(
        `${video.paused ? "Paused" : "Playing"} · ${video.currentTime.toFixed(1)} / ${Number.isFinite(video.duration) ? video.duration.toFixed(1) : "—"}s · ${video.videoWidth}×${video.videoHeight} · ${quality.totalVideoFrames} decoded frames · ${quality.droppedVideoFrames} dropped · Audio ${video.muted ? "muted" : `${(rms * 100).toFixed(2)}% RMS`} · ${context?.state ?? "idle"}`,
      );
    }, 500);
    return () => {
      clearInterval(timer);
      video.removeEventListener("playing", connect);
      void context?.close();
    };
  }, []);
  return (
    <>
      <form
        className="flex items-center gap-3 mt-4 text-sm"
        onSubmit={(event) => {
          event.preventDefault();
          const video = document.querySelector<HTMLVideoElement>("video");
          if (video && Number.isFinite(video.duration))
            video.currentTime = Math.max(
              0,
              Math.min(Number(seek), video.duration - 0.1),
            );
        }}
      >
        <label htmlFor="seek">Jump to second</label>
        <input
          id="seek"
          type="number"
          min="0"
          value={seek}
          onChange={(event) => setSeek(event.target.value)}
          className="w-20 bg-gray-800 rounded p-1"
        />
        <button className="bg-gray-800 rounded px-3 py-1" type="submit">
          Seek
        </button>
      </form>
      <output
        className="block text-xs text-gray-400 mt-4"
        aria-label="Playback verification"
      >
        {status}
      </output>
    </>
  );
}

createRoot(document.getElementById("root")!).render(
  <BrowserRouter>
    <HeadingProvider>
      <main className="max-w-4xl mx-auto px-4 sm:px-6 py-8 w-full h-screen overflow-y-auto">
        <p className="text-green-300 mb-2">Website demo preview</p>
        <h1 className="text-2xl font-bold mb-3">{candidate.title}</h1>
        <p className="text-gray-400 mb-6">
          Press Demo Video to play the full video as ASCII with audio. This uses
          the homepage’s actual widget and dimensions. Pause it before playing
          the video without ASCII below.
        </p>
        <MirrorDemoWidget
          showHeader={false}
          showDebugLogs={false}
          minHeight={375}
          demoVideo={{
            src,
            title: candidate.title,
            href: `https://www.youtube.com/watch?v=${candidate.id}`,
          }}
        />
        <PlaybackStatus />
        <details className="mt-6">
          <summary className="cursor-pointer text-cyan-300">
            Watch video without ASCII
          </summary>
          <video
            className="w-full mt-3"
            src={src}
            controls
            loop
            preload="metadata"
            playsInline
          />
        </details>
      </main>
    </HeadingProvider>
  </BrowserRouter>,
);
