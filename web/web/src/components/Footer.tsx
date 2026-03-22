import { Footer as SharedFooter } from "@ascii-chat/shared/components";
import { SITES } from "@ascii-chat/shared/utils";

export function Footer() {
  return (
    <SharedFooter
      links={[
        {
          href: SITES.MAIN,
          label: "🖥️ www",
          color: "text-terminal-green hover:text-terminal-brightGreen",
          target: "_blank",
          rel: "noopener noreferrer",
        },
        {
          href: SITES.DISCOVERY,
          label: "🔍 discovery",
          color: "text-terminal-magenta hover:text-terminal-brightMagenta",
          target: "_blank",
          rel: "noopener noreferrer",
        },
        {
          href: "https://github.com/zfogg/ascii-chat",
          label: "📦 GitHub",
          color: "text-terminal-cyan hover:text-terminal-brightCyan",
          target: "_blank",
          rel: "noopener noreferrer",
        },
      ]}
      commitSha={__COMMIT_SHA__}
      extraLine={
        <>
          ascii-chat · Video chat in your{" "}
          <span className="line-through">terminal</span> browser
        </>
      }
      authorLinkColor="text-terminal-cyan hover:text-terminal-brightCyan"
      className="border-t border-terminal-8 pt-6 md:pt-8 text-terminal-8 mt-auto mb-8"
    />
  );
}
