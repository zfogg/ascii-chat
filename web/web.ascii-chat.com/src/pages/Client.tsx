/**
 * Client WASM Demo Page
 * Demonstrates WebAssembly client mode with crypto handshake
 */

import { useEffect, useState } from 'react';
import {
  initClientWasm,
  generateKeypair,
  ConnectionState,
  isWasmReady
} from '../wasm/client';
import { ClientConnection } from '../network/ClientConnection';
import { WebClientHead } from '../components/WebClientHead';

export function ClientPage() {
  const [status, setStatus] = useState<string>('Not initialized');
  const [publicKey, setPublicKey] = useState<string>('');
  const [connectionState, setConnectionState] = useState<ConnectionState>(
    ConnectionState.DISCONNECTED
  );
  const [client, setClient] = useState<ClientConnection | null>(null);
  const [serverUrl, setServerUrl] = useState<string>('ws://localhost:27226');

  // Initialize WASM and auto-connect on mount
  useEffect(() => {
    let clientConn: ClientConnection | null = null;

    const init = async () => {
      try {
        setStatus('Initializing WASM...');
        await initClientWasm({ width: 80, height: 40 });

        if (!isWasmReady()) {
          throw new Error('WASM module not ready');
        }

        setStatus('WASM initialized successfully');

        // Auto-generate keypair
        setStatus('Generating keypair...');
        const pubkey = await generateKeypair();
        setPublicKey(pubkey);
        setStatus('Keypair generated');

        // Auto-connect to server
        setStatus('Connecting to server...');

        // Create client connection
        const conn = new ClientConnection({
          serverUrl,
          width: 80,
          height: 40
        });
        clientConn = conn;

        // Set up callbacks
        conn.onStateChange((state) => {
          setConnectionState(state);
          const stateNames = {
            [ConnectionState.DISCONNECTED]: 'Disconnected',
            [ConnectionState.CONNECTING]: 'Connecting',
            [ConnectionState.HANDSHAKE]: 'Performing handshake',
            [ConnectionState.CONNECTED]: 'Connected',
            [ConnectionState.ERROR]: 'Error'
          };
          setStatus(stateNames[state] || 'Unknown state');
        });

        conn.onPacketReceived((packet, payload) => {
          console.log('Received packet:', packet, 'payload length:', payload.length);
        });

        // Connect
        await conn.connect();
        setClient(conn);
        setPublicKey(conn.getPublicKey() || '');
      } catch (error) {
        setStatus(`Error: ${error}`);
        console.error('Failed to initialize or connect:', error);
      }
    };

    init();

    // Cleanup on unmount
    return () => {
      if (clientConn) {
        clientConn.disconnect();
      }
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []); // Only run on mount

  const handleGenerateKeypair = async () => {
    try {
      setStatus('Generating keypair...');
      const pubkey = await generateKeypair();
      setPublicKey(pubkey);
      setStatus('Keypair generated');
    } catch (error) {
      setStatus(`Error: ${error}`);
      console.error('Failed to generate keypair:', error);
    }
  };

  const handleConnect = async () => {
    try {
      setStatus('Connecting to server...');

      // Create client connection
      const conn = new ClientConnection({
        serverUrl,
        width: 80,
        height: 40
      });

      // Set up callbacks
      conn.onStateChange((state) => {
        setConnectionState(state);
        const stateNames = {
          [ConnectionState.DISCONNECTED]: 'Disconnected',
          [ConnectionState.CONNECTING]: 'Connecting',
          [ConnectionState.HANDSHAKE]: 'Performing handshake',
          [ConnectionState.CONNECTED]: 'Connected',
          [ConnectionState.ERROR]: 'Error'
        };
        setStatus(stateNames[state] || 'Unknown state');
      });

      conn.onPacketReceived((packet, payload) => {
        console.log('Received packet:', packet, 'payload length:', payload.length);
      });

      // Connect
      await conn.connect();
      setClient(conn);
      setPublicKey(conn.getPublicKey() || '');
    } catch (error) {
      setStatus(`Connection error: ${error}`);
      console.error('Failed to connect:', error);
    }
  };

  const handleDisconnect = () => {
    if (client) {
      client.disconnect();
      setClient(null);
      setConnectionState(ConnectionState.DISCONNECTED);
      setStatus('Disconnected');
    }
  };

  const getStateColor = () => {
    switch (connectionState) {
      case ConnectionState.CONNECTED:
        return 'text-green-600';
      case ConnectionState.CONNECTING:
      case ConnectionState.HANDSHAKE:
        return 'text-yellow-600';
      case ConnectionState.ERROR:
        return 'text-red-600';
      default:
        return 'text-gray-600';
    }
  };

  return (
    <>
      <WebClientHead
        title="Client Demo - ascii-chat Web Client"
        description="WebAssembly client mode with X25519 key exchange and XSalsa20-Poly1305 encryption. Test real-time encrypted video chat in your browser."
        url="https://web.ascii-chat.com/client"
      />
      <div className="flex-1 bg-gray-100 py-8 px-4">
        <div className="max-w-4xl mx-auto">
          <div className="bg-white rounded-lg shadow-lg p-8">
            <h1 className="text-3xl font-bold mb-2">ASCII Chat - Client WASM Demo</h1>
          <p className="text-gray-600 mb-8">
            WebAssembly client mode with X25519 key exchange and XSalsa20-Poly1305 encryption
          </p>

          {/* Status Section */}
          <div className="mb-8 p-4 bg-gray-50 rounded border border-gray-200">
            <h2 className="text-lg font-semibold mb-2">Status</h2>
            <p className={`status font-mono ${getStateColor()}`}>{status}</p>
          </div>

          {/* Connection Settings */}
          <div className="mb-8">
            <h2 className="text-lg font-semibold mb-4">Connection Settings</h2>
            <div className="flex gap-4">
              <input
                type="text"
                value={serverUrl}
                onChange={(e) => setServerUrl(e.target.value)}
                placeholder="ws://localhost:27224"
                className="flex-1 px-4 py-2 border border-gray-300 rounded focus:outline-none focus:ring-2 focus:ring-blue-500"
                disabled={!!client}
              />
            </div>
          </div>

          {/* Actions */}
          <div className="mb-8 space-y-4">
            <button
              onClick={handleGenerateKeypair}
              disabled={!isWasmReady() || !!client}
              className="w-full px-6 py-3 bg-blue-600 text-white rounded hover:bg-blue-700 disabled:bg-gray-300 disabled:cursor-not-allowed transition"
            >
              Generate Keypair
            </button>

            {!client ? (
              <button
                onClick={handleConnect}
                disabled={!publicKey}
                className="w-full px-6 py-3 bg-green-600 text-white rounded hover:bg-green-700 disabled:bg-gray-300 disabled:cursor-not-allowed transition"
              >
                Connect to Server
              </button>
            ) : (
              <button
                onClick={handleDisconnect}
                className="w-full px-6 py-3 bg-red-600 text-white rounded hover:bg-red-700 transition"
              >
                Disconnect
              </button>
            )}
          </div>

          {/* Public Key Display */}
          {publicKey && (
            <div className="mb-8">
              <h2 className="text-lg font-semibold mb-2">Client Public Key</h2>
              <div className="p-4 bg-gray-50 rounded border border-gray-200">
                <code className="text-sm break-all">{publicKey}</code>
              </div>
            </div>
          )}

          {/* Connection State */}
          <div className="mb-8">
            <h2 className="text-lg font-semibold mb-2">Connection State</h2>
            <div className="grid grid-cols-5 gap-2">
              {[
                { state: ConnectionState.DISCONNECTED, label: 'Disconnected' },
                { state: ConnectionState.CONNECTING, label: 'Connecting' },
                { state: ConnectionState.HANDSHAKE, label: 'Handshake' },
                { state: ConnectionState.CONNECTED, label: 'Connected' },
                { state: ConnectionState.ERROR, label: 'Error' }
              ].map(({ state, label }) => (
                <div
                  key={state}
                  className={`p-3 rounded text-center text-sm ${
                    connectionState === state
                      ? 'bg-blue-600 text-white font-semibold'
                      : 'bg-gray-200 text-gray-600'
                  }`}
                >
                  {label}
                </div>
              ))}
            </div>
          </div>

          {/* Info */}
          <div className="text-sm text-gray-600 space-y-2">
            <p>
              <strong>WASM Module:</strong> client.wasm (1.3MB with Opus codec)
            </p>
            <p>
              <strong>Crypto:</strong> X25519 key exchange, XSalsa20-Poly1305 AEAD
            </p>
            <p>
              <strong>Audio:</strong> Opus codec @ 48kHz mono
            </p>
            <p>
              <strong>Network:</strong> WebSocket with automatic reconnection
            </p>
          </div>
        </div>
      </div>
      </div>
    </>
  );
}
