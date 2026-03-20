import { Heading } from "@ascii-chat/shared/components";

export default function HeroHeader() {
  return (
    <header className="mb-12 sm:mb-16 text-center">
      <Heading
        level={1}
        className="text-4xl sm:text-5xl md:text-6xl font-bold mb-4"
        id="ascii-chat"
        anchorLink={false}
      >
        <span className="text-cyan-400">💻</span>
        <span className="text-purple-400">📸</span>{" "}
        <span className="text-cyan-400">ascii</span>
        <span className="text-gray-500">-</span>
        <span className="text-teal-400">chat</span>{" "}
        <span className="text-pink-400">🔡</span>
        <span className="text-purple-400">💬</span>
      </Heading>
      <p className="text-lg sm:text-xl md:text-2xl text-gray-300 mb-2">
        Video chat in your terminal
      </p>
      <p className="text-sm sm:text-base text-gray-400">
        Real-time terminal-based video conferencing with ASCII art rendering,
        end-to-end encryption, and audio support
      </p>
    </header>
  );
}
