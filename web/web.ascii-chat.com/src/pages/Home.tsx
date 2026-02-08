import { Heading, Button } from "@ascii-chat/shared/components";

export function HomePage() {
  return (
    <div className="flex-1 flex items-center justify-center">
      <div className="text-center max-w-2xl px-8">
        <Heading level={1} className="text-6xl font-bold text-terminal-cyan mb-4">ascii-chat</Heading>
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
        <p className="text-terminal-brightBlack mt-8 text-sm">
          Phase 0: Project scaffolding complete ✓
        </p>
      </div>
    </div>
  )
}
