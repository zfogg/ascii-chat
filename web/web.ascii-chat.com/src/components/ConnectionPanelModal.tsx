import { Dialog, DialogPanel, DialogTitle } from "@headlessui/react";
import { ConnectionState } from "../wasm/client";

export interface ConnectionPanelModalProps {
  isOpen: boolean;
  onClose: () => void;
  connectionState: ConnectionState;
  status: string;
  publicKey: string;
  serverUrl: string;
  onServerUrlChange: (url: string) => void;
  onConnect: () => void;
  onDisconnect: () => void;
  isConnected: boolean;
}

const STATE_STEPS = [
  { state: ConnectionState.DISCONNECTED, label: "Disconnected" },
  { state: ConnectionState.CONNECTING, label: "Connecting" },
  { state: ConnectionState.HANDSHAKE, label: "Handshake" },
  { state: ConnectionState.CONNECTED, label: "Connected" },
  { state: ConnectionState.ERROR, label: "Error" },
];

function getStateColor(state: ConnectionState): string {
  switch (state) {
    case ConnectionState.CONNECTED:
      return "text-terminal-2";
    case ConnectionState.CONNECTING:
    case ConnectionState.HANDSHAKE:
      return "text-terminal-3";
    case ConnectionState.ERROR:
      return "text-terminal-1";
    default:
      return "text-terminal-8";
  }
}

export function ConnectionPanelModal({
  isOpen,
  onClose,
  connectionState,
  status,
  publicKey,
  serverUrl,
  onServerUrlChange,
  onConnect,
  onDisconnect,
  isConnected,
}: ConnectionPanelModalProps) {
  return (
    <Dialog open={isOpen} onClose={onClose} className="relative z-50">
      {/* Backdrop */}
      <div className="fixed inset-0 bg-black/60" aria-hidden="true" />

      <div className="fixed inset-0 flex items-center justify-center p-4">
        <DialogPanel className="w-full max-w-lg bg-terminal-0 border border-terminal-8 rounded-lg shadow-xl">
          <div className="p-6">
            <DialogTitle className="text-lg font-semibold text-terminal-fg mb-4">
              Connection
            </DialogTitle>

            {/* Status */}
            <div className="mb-4 p-3 bg-terminal-bg rounded border border-terminal-8">
              <div className="text-xs text-terminal-8 mb-1">Status</div>
              <div
                className={`font-mono text-sm ${
                  getStateColor(connectionState)
                }`}
              >
                {status}
              </div>
            </div>

            {/* Connection state progress */}
            <div className="mb-4">
              <div className="grid grid-cols-5 gap-1">
                {STATE_STEPS.map(({ state, label }) => (
                  <div
                    key={state}
                    className={`p-2 rounded text-center text-xs ${
                      connectionState === state
                        ? "bg-terminal-4 text-terminal-bg font-semibold"
                        : "bg-terminal-bg text-terminal-8"
                    }`}
                  >
                    {label}
                  </div>
                ))}
              </div>
            </div>

            {/* Server URL */}
            <div className="mb-4">
              <label className="block text-xs font-medium text-terminal-8 mb-1">
                Server URL
              </label>
              <input
                type="text"
                value={serverUrl}
                onChange={(e) => onServerUrlChange(e.target.value)}
                placeholder="ws://localhost:27226"
                disabled={isConnected}
                className="w-full px-3 py-2 bg-terminal-bg border border-terminal-8 rounded text-sm text-terminal-fg font-mono focus:outline-none focus:border-terminal-4 disabled:opacity-50"
              />
            </div>

            {/* Connect / Disconnect */}
            <div className="mb-4">
              {!isConnected
                ? (
                  <button
                    onClick={onConnect}
                    className="w-full px-4 py-2 bg-terminal-2 text-terminal-bg rounded hover:bg-terminal-10 text-sm font-medium"
                  >
                    Connect
                  </button>
                )
                : (
                  <button
                    onClick={onDisconnect}
                    className="w-full px-4 py-2 bg-terminal-1 text-terminal-bg rounded hover:bg-terminal-9 text-sm font-medium"
                  >
                    Disconnect
                  </button>
                )}
            </div>

            {/* Public Key */}
            {publicKey && (
              <div className="mb-4">
                <div className="text-xs font-medium text-terminal-8 mb-1">
                  Client Public Key
                </div>
                <div className="p-2 bg-terminal-bg rounded border border-terminal-8">
                  <code className="text-xs text-terminal-fg break-all text-[0.70rem]">
                    {publicKey}
                  </code>
                </div>
              </div>
            )}

            {/* Info */}
            <div className="text-xs text-terminal-8 space-y-1 mb-4">
              <div>Crypto: X25519 + XSalsa20-Poly1305 AEAD</div>
              <div>Audio: Opus @ 48kHz mono</div>
            </div>

            {/* Close */}
            <button
              onClick={onClose}
              className="w-full px-4 py-2 bg-terminal-8 text-terminal-fg rounded hover:bg-terminal-7 text-sm"
            >
              Close
            </button>
          </div>
        </DialogPanel>
      </div>
    </Dialog>
  );
}
