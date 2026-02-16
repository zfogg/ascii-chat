import { WebClientHead } from "../components/WebClientHead";

export function DiscoveryPage() {
  return (
    <>
      <WebClientHead
        title="Discovery Mode - ascii-chat Web Client"
        description="WebRTC P2P connections for ascii-chat. Connect directly with peers using session discovery."
        url="https://web.ascii-chat.com/discovery"
      />
      <div className="flex-1 flex items-center justify-center">
        <div className="text-center">
          <h1 className="text-4xl font-bold text-terminal-magenta mb-4">
            ascii-chat | Discovery Mode
          </h1>
          <p className="text-terminal-fg mb-8">
            Coming soon: WebRTC P2P connections
          </p>
        </div>
      </div>
    </>
  );
}
