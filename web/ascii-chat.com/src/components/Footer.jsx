import TrackedLink from "./TrackedLink";

export default function Footer() {
  return (
    <footer className="border-t border-gray-800 pt-8 text-gray-500 mb-8">
      <div className="flex justify-center gap-6 sm:gap-8 mb-4 flex-wrap">
        <TrackedLink
          href="https://github.com/zfogg/ascii-chat"
          label="Footer - GitHub"
          className="text-cyan-400 hover:text-cyan-300 transition-colors"
        >
          📦&nbsp;&nbsp;GitHub
        </TrackedLink>
        <TrackedLink
          href="https://github.com/zfogg/ascii-chat/issues"
          label="Footer - Issues"
          className="text-purple-400 hover:text-purple-300 transition-colors"
        >
          🐛&nbsp;&nbsp;Issues
        </TrackedLink>
        <TrackedLink
          href="https://github.com/zfogg/ascii-chat/releases"
          label="Footer - Releases"
          className="text-teal-400 hover:text-teal-300 transition-colors"
        >
          📦&nbsp;&nbsp;Releases
        </TrackedLink>
        <TrackedLink
          href="https://discovery.ascii-chat.com"
          label="Footer - ACDS"
          target="_blank"
          rel="noopener noreferrer"
          className="text-pink-400 hover:text-pink-300 transition-colors"
        >
          🔍&nbsp;&nbsp;ACDS
        </TrackedLink>
      </div>
      <div className="text-center">
        <p className="text-sm">ascii-chat · Video chat in your terminal</p>
        <p className="text-sm mt-2">
          made with 💖 by{" "}
          <TrackedLink
            href="https://zfo.gg"
            label="Footer - Author"
            target="_blank"
            rel="noopener noreferrer"
            className="text-cyan-400 hover:text-cyan-300 transition-colors"
          >
            @zfogg
          </TrackedLink>
        </p>
      </div>
    </footer>
  );
}
