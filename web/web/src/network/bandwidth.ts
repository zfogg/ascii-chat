// Count application bytes at the transport boundary, before receive reassembly
// and after successful sends. Browser transport/TLS headers are not included.
let uploadedBytes = 0;
let downloadedBytes = 0;

export function recordUpload(bytes: number): void {
  uploadedBytes += bytes;
}

export function recordDownload(bytes: number): void {
  downloadedBytes += bytes;
}

export function createBandwidthSampler() {
  let previousUpload = uploadedBytes;
  let previousDownload = downloadedBytes;
  let previousTime = performance.now();

  return () => {
    const now = performance.now();
    const seconds = (now - previousTime) / 1000;
    if (seconds <= 0) return { upload: 0, download: 0 };
    const rates = {
      upload: (uploadedBytes - previousUpload) / seconds,
      download: (downloadedBytes - previousDownload) / seconds,
    };
    previousUpload = uploadedBytes;
    previousDownload = downloadedBytes;
    previousTime = now;
    return rates;
  };
}

export function formatBandwidth(bytesPerSecond: number): string {
  if (bytesPerSecond >= 1_000_000)
    return `${(bytesPerSecond / 1_000_000).toFixed(1)} MB/s`;
  if (bytesPerSecond >= 1_000)
    return `${(bytesPerSecond / 1_000).toFixed(1)} kB/s`;
  return `${Math.round(bytesPerSecond)} B/s`;
}
