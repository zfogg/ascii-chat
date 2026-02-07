export function Footer() {
  return (
    <footer className="border-t border-terminal-8 pt-8 pb-8 text-terminal-8 mt-auto">
      <div className="flex justify-center gap-6 sm:gap-8 mb-4 flex-wrap">
        <a
          href="https://github.com/zfogg/ascii-chat"
          target="_blank"
          rel="noopener noreferrer"
          className="text-terminal-cyan hover:text-terminal-brightCyan transition-colors"
        >
          📦&nbsp;&nbsp;GitHub
        </a>
        <a
          href="https://ascii-chat.com"
          target="_blank"
          rel="noopener noreferrer"
          className="text-terminal-green hover:text-terminal-brightGreen transition-colors"
        >
          🖥️&nbsp;&nbsp;ascii-chat.com
        </a>
        <a
          href="https://discovery.ascii-chat.com"
          target="_blank"
          rel="noopener noreferrer"
          className="text-terminal-magenta hover:text-terminal-brightMagenta transition-colors"
        >
          🔍&nbsp;&nbsp;ACDS
        </a>
      </div>
      <div className="text-center">
        <p className="text-sm">
          ascii-chat · Video chat in your <span className="line-through">terminal</span> browser
        </p>
        <p className="text-sm mt-2">
          made with ❤️ by{" "}
          <a
            href="https://zfo.gg"
            target="_blank"
            rel="noopener noreferrer"
            className="text-terminal-cyan hover:text-terminal-brightCyan transition-colors"
          >
            @zfogg
          </a>
          {" · "}
          <span className="text-terminal-8 font-mono text-xs">
            {__COMMIT_SHA__}
          </span>
        </p>
      </div>
    </footer>
  )
}
