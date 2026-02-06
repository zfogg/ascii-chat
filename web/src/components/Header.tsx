export function Header() {
  return (
    <header className="border-b border-terminal-8 bg-terminal-0 px-4 py-3">
      <div className="flex items-center justify-between">
        <a href="/" className="text-xl font-bold text-terminal-cyan hover:text-terminal-brightCyan transition-colors">
          ascii-chat
        </a>
        <nav className="flex gap-4 text-sm">
          <a
            href="/"
            className="text-terminal-fg hover:text-terminal-cyan transition-colors"
          >
            Home
          </a>
          <a
            href="/mirror"
            className="text-terminal-fg hover:text-terminal-cyan transition-colors"
          >
            Mirror
          </a>
          <a
            href="/client"
            className="text-terminal-fg hover:text-terminal-cyan transition-colors"
          >
            Client
          </a>
          <a
            href="/discovery"
            className="text-terminal-fg hover:text-terminal-cyan transition-colors"
          >
            Discovery
          </a>
        </nav>
      </div>
    </header>
  )
}
