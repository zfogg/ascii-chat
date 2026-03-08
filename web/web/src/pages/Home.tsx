import { Heading, Button } from "@ascii-chat/shared/components";
import { WebClientHead } from "../components/WebClientHead";

export function HomePage() {
  return (
    <div className="flex-1 flex items-center justify-center pt-8">
      <WebClientHead title="ascii-chat Web Client - Video chat in your terminal/browser" />
      <div className="text-center max-w-2xl px-8">
        <Heading
          level={1}
          className="text-4xl sm:text-6xl font-bold text-terminal-cyan mb-2"
        >
          ascii-chat
        </Heading>
        <p className="text-sm md:text-base text-terminal-8 font-normal mb-6">
          (Web Client)
        </p>
        <p className="text-xl text-terminal-fg mb-8">
          Video chat in your <del className="opacity-60">terminal</del> browser
        </p>
        <div className="space-y-4 flex flex-col items-center max-w-4xl">
          <div className="flex items-center gap-4 w-full justify-center">
            <Button
              href="/mirror"
              className="w-56 px-6 py-3 bg-terminal-cyan text-terminal-bg rounded hover:opacity-80 transition-opacity"
            >
              Mirror Mode
            </Button>
            <p className="text-sm text-terminal-cyan flex-1 text-left">
              Use your webcam, files, or URLs without network activity
            </p>
          </div>
          <div className="flex items-center gap-4 w-full justify-center">
            <Button
              href="/client"
              className="w-56 px-6 py-3 bg-terminal-green text-terminal-bg rounded hover:opacity-80 transition-opacity"
            >
              Client Mode
            </Button>
            <p className="text-sm text-terminal-green flex-1 text-left">
              Connect to ascii-chat servers via WebSocket and URL or IP
            </p>
          </div>
          <div className="flex items-center gap-4 w-full justify-center">
            <Button
              href="/discovery"
              className="w-56 px-6 py-3 bg-terminal-magenta text-terminal-bg rounded hover:opacity-80 transition-opacity"
            >
              Discovery Mode
            </Button>
            <p className="text-sm text-terminal-magenta flex-1 text-left">
              Get a shareable link for someone to see your ASCII art face.
            </p>
          </div>
        </div>
      </div>
    </div>
  );
}
