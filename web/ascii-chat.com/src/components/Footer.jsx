import TrackedLink from "./TrackedLink";
import { Footer as SharedFooter } from "@ascii-chat/shared/components";

export default function Footer() {
  const trackClick = (label) => {
    // Analytics tracking is handled by TrackedLink wrapping
  };

  return (
    <SharedFooter
      links={[
        {
          href: "https://github.com/zfogg/ascii-chat",
          label: "📦 GitHub",
          color: "text-cyan-400 hover:text-cyan-300",
        },
        {
          href: "https://github.com/zfogg/ascii-chat/issues",
          label: "🐛 Issues",
          color: "text-purple-400 hover:text-purple-300",
        },
        {
          href: "https://github.com/zfogg/ascii-chat/releases",
          label: "📦 Releases",
          color: "text-teal-400 hover:text-teal-300",
        },
        {
          href: "https://discovery.ascii-chat.com",
          label: "🔍 ACDS",
          color: "text-pink-400 hover:text-pink-300",
        },
        {
          href: "https://web.ascii-chat.com",
          label: "🌐 Web Client",
          color: "text-yellow-400 hover:text-yellow-300",
        },
      ]}
      commitSha={__COMMIT_SHA__}
      extraLine="ascii-chat · Video chat in your terminal"
    />
  );
}
