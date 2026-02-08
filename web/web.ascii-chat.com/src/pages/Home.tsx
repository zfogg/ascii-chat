import { Heading, Button } from "@ascii-chat/shared/components";
import { WebClientHead } from "../components/WebClientHead";

export function HomePage() {
  return (
    <div className="flex-1 flex items-center justify-center pt-8">
      <WebClientHead
        title="ascii-chat Web Client - Video chat in your terminal/browser"
      />
      <div className="text-center max-w-2xl px-8">
        <Heading level={1} className="text-4xl sm:text-6xl font-bold text-terminal-cyan mb-2">ascii-chat</Heading>
        <p className="text-sm md:text-base text-terminal-8 font-normal mb-6">(Web Client)</p>
        <p className="text-xl text-terminal-fg mb-8">
          Video chat in your <del className="opacity-60">terminal</del> browser
        </p>
        <div className="space-y-4 flex flex-col items-center">
          <Button
            href="/mirror"
            className="px-6 py-3 bg-terminal-cyan text-terminal-bg rounded hover:opacity-80 transition-opacity inline-block w-64"
          >
            Mirror Mode
          </Button>
          <Button
            href="/client"
            className="px-6 py-3 bg-terminal-green text-terminal-bg rounded hover:opacity-80 transition-opacity inline-block w-64"
          >
            Client Mode
          </Button>
          <Button
            href="/discovery"
            className="px-6 py-3 bg-terminal-magenta text-terminal-bg rounded hover:opacity-80 transition-opacity inline-block w-64"
          >
            Discovery Mode
          </Button>
        </div>
      </div>
    </div>
  )
}
