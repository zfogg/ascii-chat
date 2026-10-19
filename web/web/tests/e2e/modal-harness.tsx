import { useState } from "react";
import { createRoot } from "react-dom/client";
import { DeviceSetupModal } from "../../src/components/DeviceSetupModal";
import { SecuritySetupModal } from "../../src/components/SecuritySetupModal";
import { VideoUploadModal } from "../../src/components/VideoUploadModal";
import { ConnectionPanelModal } from "../../src/components/ConnectionPanelModal";
import { ConnectionState } from "../../src/wasm/client";
import { DEFAULT_CRYPTO_SETTINGS } from "../../src/utils/cryptoSettings";
import "../../src/style.css";

function Harness() {
  const [open, setOpen] = useState(false);
  const close = () => setOpen(false);
  const modal = new URLSearchParams(window.location.search).get("modal");
  return (
    <main style={{ minHeight: "200vh" }}>
      <button onClick={() => setOpen(true)}>Open modal</button>
      {modal === "device" && <DeviceSetupModal open={open} onClose={close} />}
      {modal === "crypto" && (
        <SecuritySetupModal
          open={open}
          onClose={close}
          settings={DEFAULT_CRYPTO_SETTINGS}
          onSave={close}
        />
      )}
      {modal === "upload" && (
        <VideoUploadModal
          isOpen={open}
          onClose={close}
          onFileSelect={close}
          allowImages
        />
      )}
      {modal === "connection" && (
        <ConnectionPanelModal
          isOpen={open}
          onClose={close}
          connectionState={ConnectionState.DISCONNECTED}
          status="Disconnected"
          publicKey={"0123456789abcdef".repeat(8)}
          serverUrl="ws://localhost:27226"
          onServerUrlChange={() => {}}
          onConnect={() => {}}
          onDisconnect={() => {}}
          isConnected={false}
        />
      )}
    </main>
  );
}

createRoot(document.getElementById("app")!).render(<Harness />);
