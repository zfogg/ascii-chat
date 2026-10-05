import { useCallback, useEffect, useRef, useState } from "react";

type View = "waveform" | "fft";

const WIDTH = 96;
const HEIGHT = 22;
const GLYPHS = " .:-=+*#%@";
const VIDEO_SRC = "/assets/demo-video-sun-models.webm";

function colorForFrequency(normalized: number): string {
  const stops: Array<[number, [number, number, number]]> = [
    [0, [255, 55, 112]],
    [0.17, [255, 112, 165]],
    [0.34, [143, 105, 255]],
    [0.52, [70, 174, 255]],
    [0.7, [79, 221, 194]],
    [0.84, [123, 235, 109]],
    [1, [255, 221, 94]],
  ];
  const value = Math.max(0, Math.min(1, normalized));
  let index = 0;
  while (index < stops.length - 2 && value > stops[index + 1][0]) index++;
  const [leftAt, left] = stops[index];
  const [rightAt, right] = stops[index + 1];
  const t = (value - leftAt) / (rightAt - leftAt);
  const rgb = left.map((channel, i) =>
    Math.round(channel + (right[i] - channel) * t),
  );
  return `rgb(${rgb.join(",")})`;
}

function drawFrame(
  canvas: HTMLCanvasElement,
  analyser: AnalyserNode,
  view: View,
  timeData: Uint8Array<ArrayBuffer>,
  frequencyData: Uint8Array<ArrayBuffer>,
) {
  const context = canvas.getContext("2d");
  if (!context) return;
  const dpr = window.devicePixelRatio || 1;
  const rect = canvas.getBoundingClientRect();
  const pixelWidth = Math.max(1, Math.floor(rect.width * dpr));
  const pixelHeight = Math.max(1, Math.floor(rect.height * dpr));
  if (canvas.width !== pixelWidth || canvas.height !== pixelHeight) {
    canvas.width = pixelWidth;
    canvas.height = pixelHeight;
  }
  context.setTransform(dpr, 0, 0, dpr, 0, 0);
  context.fillStyle = "#090d14";
  context.fillRect(0, 0, rect.width, rect.height);

  const cellWidth = rect.width / WIDTH;
  const cellHeight = rect.height / HEIGHT;
  context.font = `${Math.max(8, cellHeight * 0.92)}px ui-monospace, SFMono-Regular, Menlo, monospace`;
  context.textBaseline = "top";
  context.shadowBlur = 7;

  if (view === "waveform") {
    analyser.getByteTimeDomainData(timeData);
    analyser.getByteFrequencyData(frequencyData);
    let weightedFrequency = 0;
    let totalEnergy = 0;
    for (let i = 0; i < frequencyData.length; i++) {
      const energy = frequencyData[i] * frequencyData[i];
      weightedFrequency += (i / frequencyData.length) * energy;
      totalEnergy += energy;
    }
    const signalColor = colorForFrequency(
      totalEnergy > 0 ? weightedFrequency / totalEnergy : 0.5,
    );
    for (let x = 0; x < WIDTH; x++) {
      const sampleIndex = Math.floor((x / WIDTH) * timeData.length);
      const sample = (timeData[sampleIndex] - 128) / 128;
      const centerY = (HEIGHT - 1) / 2;
      const amplitude = Math.abs(sample) * (HEIGHT * 0.47);
      const tipY = Math.max(
        0,
        Math.min(HEIGHT - 1, Math.round(centerY - sample * (HEIGHT * 0.47))),
      );
      const color = signalColor;
      for (let y = 0; y < HEIGHT; y++) {
        const distance = Math.abs(y - centerY);
        if (distance <= amplitude && amplitude > 0.2) {
          const intensity = 1 - distance / Math.max(1, amplitude);
          const glyph =
            GLYPHS[
              Math.min(
                GLYPHS.length - 1,
                Math.floor(intensity * (GLYPHS.length - 1)),
              )
            ];
          context.fillStyle = color;
          context.shadowColor = color;
          context.globalAlpha = 0.5 + intensity * 0.5;
          context.fillText(glyph, x * cellWidth, y * cellHeight);
        }
      }
      context.globalAlpha = 1;
      if (tipY >= 0) {
        context.fillStyle = color;
        context.shadowColor = color;
        context.fillText("●", x * cellWidth, tipY * cellHeight);
      }
    }
  } else {
    analyser.getByteFrequencyData(frequencyData);
    for (let x = 0; x < WIDTH; x++) {
      const bin = Math.max(0, Math.floor((x / WIDTH) * frequencyData.length));
      const value = frequencyData[bin] / 255;
      const barHeight = Math.round(value * (HEIGHT - 2));
      const color = colorForFrequency(x / WIDTH);
      for (let y = HEIGHT - 1; y >= HEIGHT - barHeight; y--) {
        const level = (HEIGHT - y) / HEIGHT;
        const glyph =
          GLYPHS[
            Math.min(
              GLYPHS.length - 1,
              Math.floor(level * (GLYPHS.length - 1)),
            )
          ];
        context.fillStyle = color;
        context.shadowColor = color;
        context.globalAlpha = 0.6 + level * 0.4;
        context.fillText(glyph, x * cellWidth, y * cellHeight);
      }
    }
    context.globalAlpha = 1;
  }
  context.shadowBlur = 0;
}

export default function AudioVisualizationDemo() {
  const [view, setView] = useState<View>("waveform");
  const [playing, setPlaying] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const videoRef = useRef<HTMLVideoElement>(null);
  const audioContextRef = useRef<AudioContext | null>(null);
  const analyserRef = useRef<AnalyserNode | null>(null);
  const frameRef = useRef<number>(0);

  const stop = useCallback(() => {
    cancelAnimationFrame(frameRef.current);
    videoRef.current?.pause();
    setPlaying(false);
  }, []);

  const start = useCallback(async () => {
    const video = videoRef.current;
    if (!video) return;
    setError(null);
    try {
      let audioContext = audioContextRef.current;
      if (!audioContext) {
        audioContext = new AudioContext();
        const source = audioContext.createMediaElementSource(video);
        const analyser = audioContext.createAnalyser();
        analyser.fftSize = 2048;
        analyser.smoothingTimeConstant = 0.78;
        source.connect(analyser);
        analyser.connect(audioContext.destination);
        audioContextRef.current = audioContext;
        analyserRef.current = analyser;
      }
      await audioContext.resume();
      if (video.ended) video.currentTime = 0;
      await video.play();
      setPlaying(true);
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  }, []);

  useEffect(() => {
    if (!playing) return;
    const analyser = analyserRef.current;
    const canvas = canvasRef.current;
    if (!analyser || !canvas) return;
    const timeData = new Uint8Array(new ArrayBuffer(analyser.fftSize));
    const frequencyData = new Uint8Array(
      new ArrayBuffer(analyser.frequencyBinCount),
    );
    const animate = () => {
      drawFrame(canvas, analyser, view, timeData, frequencyData);
      frameRef.current = requestAnimationFrame(animate);
    };
    animate();
    return () => cancelAnimationFrame(frameRef.current);
  }, [playing, view]);

  useEffect(() => () => {
    cancelAnimationFrame(frameRef.current);
    void audioContextRef.current?.close();
  }, []);

  return (
    <div className="mt-8 rounded-xl border border-slate-700 bg-slate-950 p-4 sm:p-5">
      <div className="mb-3 flex flex-wrap items-center justify-between gap-3">
        <div>
          <h3 className="text-lg font-semibold text-slate-100">Audio visualization examples</h3>
          <p className="text-sm text-slate-400">Hear the demo track and watch its live audio signal.</p>
        </div>
        <div className="flex gap-2">
          <button onClick={() => setView("waveform")} className={`rounded px-3 py-1.5 text-sm ${view === "waveform" ? "bg-fuchsia-700 text-white" : "bg-slate-800 text-slate-300 hover:bg-slate-700"}`}>
            --waveform
          </button>
          <button onClick={() => setView("fft")} className={`rounded px-3 py-1.5 text-sm ${view === "fft" ? "bg-sky-700 text-white" : "bg-slate-800 text-slate-300 hover:bg-slate-700"}`}>
            --fft
          </button>
        </div>
      </div>
      <canvas ref={canvasRef} aria-label={`${view} audio visualization`} className="block h-64 w-full rounded bg-[#090d14] sm:h-72" />
      <video ref={videoRef} src={VIDEO_SRC} loop playsInline preload="none" className="hidden" />
      <div className="mt-3 flex flex-wrap items-center gap-3">
        {!playing ? (
          <button onClick={() => void start()} className="rounded bg-emerald-700 px-4 py-2 text-sm font-medium text-white hover:bg-emerald-600">Play audio visualization</button>
        ) : (
          <button onClick={stop} className="rounded bg-slate-700 px-4 py-2 text-sm font-medium text-white hover:bg-slate-600">Stop</button>
        )}
        <code className="text-xs text-slate-400">ascii-chat mirror --file music.mp4 --audio-source media {view === "waveform" ? "--waveform" : "--fft"}</code>
      </div>
      {error && <p role="alert" className="mt-2 text-sm text-red-400">Could not play demo audio: {error}</p>}
      <p className="mt-2 text-xs text-slate-500">The live browser demo visualizes the demo track; the command shows the equivalent native CLI flag.</p>
    </div>
  );
}
