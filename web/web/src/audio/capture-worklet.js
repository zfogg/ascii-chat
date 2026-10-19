const { AudioWorkletProcessor, registerProcessor } = globalThis;
/** Capture on the audio clock; transfer reusable frames directly to the DSP worker. */
class PcmCapture extends AudioWorkletProcessor {
  constructor() {
    super();
    this.frames = Array.from({ length: 8 }, () => new Float32Array(960));
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
  process(inputs) {
    const input = inputs[0]?.[0];
    if (!input || !this.capturePort) return true;
    for (const sample of input) {
      if (!this.pending) this.pending = this.frames.pop();
      // Drop on overload instead of growing an unbounded latency queue.
      if (!this.pending) continue;
      this.pending[this.offset++] = sample;
      if (this.offset === this.pending.length) {
        this.capturePort.postMessage(this.pending, [this.pending.buffer]);
        this.pending = null;
        this.offset = 0;
      }
    }
    return true;
  }
}
registerProcessor("ascii-chat-pcm-capture", PcmCapture);
