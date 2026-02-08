import { Footer as SharedFooter } from "@ascii-chat/shared/components";

export function Footer() {
  return (
    <SharedFooter
      links={[
        { href: 'https://ascii-chat.com', label: '🖥️ www', color: 'text-terminal-green hover:text-terminal-brightGreen' },
        { href: 'https://discovery.ascii-chat.com', label: '🔍 discovery', color: 'text-terminal-magenta hover:text-terminal-brightMagenta' },
        { href: 'https://github.com/zfogg/ascii-chat', label: '📦 GitHub', color: 'text-terminal-cyan hover:text-terminal-brightCyan' },
      ]}
      commitSha={__COMMIT_SHA__}
      extraLine={<>ascii-chat · Video chat in your <span className="line-through">terminal</span> browser</>}
      authorLinkColor="text-terminal-cyan hover:text-terminal-brightCyan"
      className="border-t border-terminal-8 pt-6 pb-6 md:pt-8 md:pb-8 text-terminal-8 mt-auto mb-8"
    />
  );
}
