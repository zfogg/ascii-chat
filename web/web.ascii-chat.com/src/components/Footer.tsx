import { CommitLink } from "@ascii-chat/shared/components";

export function Footer() {
  return (
    <footer className="border-t border-terminal-8 pt-6 pb-6 md:pt-8 md:pb-8 text-terminal-8 mt-auto">
      <div className="flex justify-center gap-4 sm:gap-6 md:gap-8 mb-3 md:mb-4 flex-wrap text-sm md:text-base">
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
        <p className="text-xs md:text-sm">
          ascii-chat · Video chat in your <span className="line-through">terminal</span> browser
        </p>
        <p className="text-xs md:text-sm mt-2">
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
          <CommitLink
            commitSha={__COMMIT_SHA__}
            className="text-terminal-cyan hover:text-terminal-brightCyan transition-colors font-mono text-xs"
          />
        </p>
      </div>
    </footer>
  )
}
