import workletUrl from "./visualization-worklet.js?url";

/** Attach a continuous, ordered PCM tap to a 48 kHz audio graph. */
export function captureVisualizationAudio(
  context: AudioContext,
  source: AudioNode,
  onSamples: (samples: Float32Array) => void,
): () => void {
  let disposed = false;
  let node: AudioWorkletNode | null = null;
  void context.audioWorklet
    .addModule(workletUrl)
    .then(() => {
      if (disposed) return;
      node = new AudioWorkletNode(context, "visualization-pcm");
      node.port.onmessage = (event: MessageEvent<Float32Array>) => {
        if (!disposed) onSamples(event.data);
      };
      source.connect(node);
      node.connect(context.destination);
    })
    .catch((error: unknown) => {
      if (!disposed) console.error("Audio visualization capture failed", error);
    });
  return () => {
    disposed = true;
    if (node) {
      source.disconnect(node);
      node.port.onmessage = null;
      node.port.close();
      node.disconnect();
    }
  };
}
