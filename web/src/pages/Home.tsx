export function HomePage() {
  return (
    <div className="h-screen flex items-center justify-center">
      <div className="text-center max-w-2xl px-8">
        <h1 className="text-6xl font-bold text-terminal-cyan mb-4">ascii-chat</h1>
        <p className="text-xl text-terminal-fg mb-8">
          Video chat in your <del className="opacity-60">terminal</del> browser
        </p>
        <div className="space-y-4 flex flex-col items-center">
          <a
            href="/mirror"
            className="px-6 py-3 bg-terminal-cyan text-terminal-bg rounded hover:opacity-80 transition-opacity inline-block w-64"
          >
            Mirror Mode
          </a>
          <a
            href="/client"
            className="px-6 py-3 bg-terminal-green text-terminal-bg rounded hover:opacity-80 transition-opacity inline-block w-64"
          >
            Client Mode
          </a>
          <a
            href="/discovery"
            className="px-6 py-3 bg-terminal-magenta text-terminal-bg rounded hover:opacity-80 transition-opacity inline-block w-64"
          >
            Discovery Mode
          </a>
        </div>
        <p className="text-terminal-brightBlack mt-8 text-sm">
          Phase 0: Project scaffolding complete ✓
        </p>
      </div>
    </div>
  )
}
