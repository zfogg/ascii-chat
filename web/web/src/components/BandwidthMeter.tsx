import { useEffect, useState } from "react";
import { createBandwidthSampler, formatBandwidth } from "../network/bandwidth";

export function BandwidthMeter() {
  const [rates, setRates] = useState({ upload: 0, download: 0 });

  useEffect(() => {
    const sample = createBandwidthSampler();
    const timer = window.setInterval(() => setRates(sample()), 1000);
    return () => window.clearInterval(timer);
  }, []);

  return (
    <span
      className="text-xs text-terminal-8 whitespace-nowrap tabular-nums"
      aria-label={`Bandwidth: upload ${formatBandwidth(rates.upload)}, download ${formatBandwidth(rates.download)}`}
      title="Upload / download across this page's WebSocket and WebRTC connections. Updated every second; excludes transport overhead."
    >
      ↑ {formatBandwidth(rates.upload)} · ↓ {formatBandwidth(rates.download)}
    </span>
  );
}
