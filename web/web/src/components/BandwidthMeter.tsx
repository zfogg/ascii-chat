import { useEffect, useState } from "react";
import { createBandwidthSampler, formatBandwidth } from "../network/bandwidth";
import { HelpIcon } from "./HelpIcon";

export function BandwidthMeter() {
  const [rates, setRates] = useState({ upload: 0, download: 0 });

  useEffect(() => {
    const sample = createBandwidthSampler();
    const timer = window.setInterval(() => setRates(sample()), 1000);
    return () => window.clearInterval(timer);
  }, []);

  return (
    <span
      className="text-xs text-terminal-8 whitespace-nowrap tabular-nums inline-flex items-center gap-1 w-[27ch] shrink-0"
      aria-label={`Bandwidth: upload ${formatBandwidth(rates.upload)}, download ${formatBandwidth(rates.download)}`}
    >
      ↑ {formatBandwidth(rates.upload)} · ↓ {formatBandwidth(rates.download)}
      <HelpIcon
        label="Bandwidth"
        text="Upload / download across this page's WebSocket and WebRTC connections. Updated every second; excludes transport overhead."
      />
    </span>
  );
}
