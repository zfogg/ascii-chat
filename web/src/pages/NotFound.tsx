export function NotFoundPage() {
  return (
    <div className="min-h-screen bg-terminal-bg flex items-center justify-center p-4">
      <div className="text-center max-w-md">
        <pre className="text-terminal-1 text-6xl font-mono mb-4">404</pre>
        <h1 className="text-2xl font-bold text-terminal-fg mb-2">Page not found</h1>
        <p className="text-terminal-8 mb-8">
          The page you're looking for doesn't exist or has been moved.
        </p>
        <a
          href="/"
          className="inline-block px-6 py-3 bg-terminal-cyan text-terminal-bg rounded hover:opacity-80 transition-opacity font-mono"
        >
          ← Back to home
        </a>
      </div>
    </div>
  )
}
