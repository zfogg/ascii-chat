const CAPTURE_FRAME_POOL_SIZE = 8;
const { AudioWorkletProcessor, registerProcessor } = globalThis;
/** Capture on the audio clock; transfer reusable frames directly to the DSP worker. */
class PcmCapture extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const layout = options?.processorOptions || {};
    this.firstChannel = layout.firstChannel || 0;
    this.microphoneChannels = layout.microphoneChannels === 2 ? 2 : 1;
    this.frameSamples = layout.frameSamples * 2;
    this.frames = Array.from({ length: CAPTURE_FRAME_POOL_SIZE }, () => ({
      microphone: new Float32Array(this.frameSamples),
      media: new Float32Array(this.frameSamples),
    }));
    this.pending = this.frames.pop();
    this.offset = 0;
    this.capturePort = null;
    this.port.onmessage = ({ data }) => {
      if (data.port) {
        this.capturePort = data.port;
        this.capturePort.onmessage = ({ data: frame }) =>
          this.frames.push(frame);
        this.capturePort.start();
      }
    };
  }
  process(inputs, outputs) {
    const microphone = inputs[0] || [];
    const media = inputs[1] || [];
    const frames = microphone[0]?.length || media[0]?.length || 0;
    if (!frames || !this.capturePort) return true;
    if (microphone.length && this.firstChannel >= microphone.length)
      throw new Error("Selected microphone input channel is unavailable");
    if (
      microphone.length > 1 &&
      this.microphoneChannels === 2 &&
      this.firstChannel + 1 >= microphone.length
    )
      throw new Error("Selected microphone stereo pair is unavailable");
    for (let frame = 0; frame < frames; frame++) {
      const left = microphone[this.firstChannel]?.[frame] || 0;
      const right =
        this.microphoneChannels === 2
          ? (microphone[this.firstChannel + 1]?.[frame] ?? left)
          : left;
      const mediaLeft = media[0]?.[frame] || 0;
      const mediaRight = media[1]?.[frame] ?? media[0]?.[frame] ?? 0;
      // Output zero stays silent for scheduling. Only the separate recording
      // bus receives the routed inputs; it never connects to loudspeakers.
      const recording = outputs?.[1];
      if (recording?.[0])
        recording[0][frame] = Math.max(-1, Math.min(1, left + mediaLeft));
      if (recording?.[1])
        recording[1][frame] = Math.max(-1, Math.min(1, right + mediaRight));
      if (!this.pending) this.pending = this.frames.pop();
      // Drop on overload instead of growing an unbounded latency queue.
      if (!this.pending) continue;
      this.pending.microphone[this.offset] = left;
      this.pending.microphone[this.offset + 1] = right;
      this.pending.media[this.offset] = mediaLeft;
      this.pending.media[this.offset + 1] = mediaRight;
      this.offset += 2;
      if (this.offset === this.frameSamples) {
        this.capturePort.postMessage(this.pending, [
          this.pending.microphone.buffer,
          this.pending.media.buffer,
        ]);
        this.pending = null;
        this.offset = 0;
      }
    }
    return true;
  }
}
registerProcessor("ascii-chat-pcm-capture", PcmCapture);
