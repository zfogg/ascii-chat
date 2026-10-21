export interface AudioLevels {
  microphone: number;
  playback: number;
  sent: number;
  played: number;
  playedSamples: number;
  underruns: number;
}

export function MediaAudioControls({
  speakers,
  microphone,
  onSpeakers,
  onMicrophone,
  levels,
}: {
  speakers: boolean;
  microphone: boolean;
  onSpeakers: () => void;
  onMicrophone: () => void;
  levels: AudioLevels;
}) {
  return (
    <>
      <button
        onClick={onSpeakers}
        className={`border rounded px-3 py-1 cursor-pointer text-white ${speakers ? "border-red-700 bg-red-700 hover:bg-red-800" : "border-terminal-8 bg-transparent"}`}
      >
        {speakers ? "Disable speakers" : "Enable speakers"}
      </button>
      <button
        onClick={onMicrophone}
        className={`border rounded px-3 py-1 cursor-pointer text-white ${microphone ? "border-red-700 bg-red-700 hover:bg-red-800" : "border-terminal-8 bg-transparent"}`}
      >
        {microphone ? "Disable microphone" : "Enable microphone"}
      </button>
      <output
        data-testid="audio-levels"
        data-sent={levels.sent}
        data-played={levels.played}
        data-played-samples={levels.playedSamples}
        data-underruns={levels.underruns}
        data-input-level={levels.microphone}
        data-playback-level={levels.playback}
        className="self-center text-sm text-terminal-8"
      >
        Input {Math.round(levels.microphone * 100)}% &middot; Playback{" "}
        {Math.round(levels.playback * 100)}%
      </output>
    </>
  );
}
