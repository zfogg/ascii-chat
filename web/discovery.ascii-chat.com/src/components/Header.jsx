export default function Header() {
  return (
    <header className="border-b border-gray-800 bg-gray-950/50 backdrop-blur-sm">
      <div className="max-w-5xl mx-auto px-4 sm:px-6 py-3 sm:py-4">
        <a
          href="/"
          className="text-xl sm:text-2xl font-bold text-cyan-400 hover:text-cyan-300 transition-colors"
        >
          🔍 ascii-chat Discovery Service
        </a>
      </div>
    </header>
  );
}
