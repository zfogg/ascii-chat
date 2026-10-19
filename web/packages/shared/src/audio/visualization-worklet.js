const { AudioWorkletProcessor, registerProcessor } = globalThis;
// The audio render thread owns sample ordering; display cadence never samples audio.
class VisualizationPcmProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.samples = new Float32Array(1024);
    this.used = 0;
  }

  process(inputs) {
    const channels = inputs[0];
    if (!channels?.length) return true;
    for (let i = 0; i < channels[0].length; i++) {
      let mono = 0;
      for (const channel of channels) mono += channel[i];
      this.samples[this.used++] = mono / channels.length;
      if (this.used === this.samples.length) {
        this.port.postMessage(this.samples, [this.samples.buffer]);
        this.samples = new Float32Array(1024);
        this.used = 0;
      }
    }
    // Output stays silent: media has its own audible connection and microphones
    // must never be monitored through the speakers.
    return true;
  }
}
registerProcessor("visualization-pcm", VisualizationPcmProcessor);
