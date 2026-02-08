import { Heading, Button } from "@ascii-chat/shared/components";

export function NotFoundPage() {
  return (
    <div className="flex-1 bg-terminal-bg flex items-center justify-center p-4">
      <div className="text-center max-w-md">
        <pre className="text-terminal-1 text-6xl font-mono mb-4">404</pre>
        <Heading level={1} className="text-2xl font-bold text-terminal-fg mb-2">Page not found</Heading>
        <p className="text-terminal-8 mb-8">
          The page you're looking for doesn't exist or has been moved.
        </p>
        <Button
          href="/"
          className="inline-block px-6 py-3 bg-terminal-cyan text-terminal-bg rounded hover:opacity-80 transition-opacity font-mono"
        >
          ← Back to home
        </Button>
      </div>
    </div>
  )
}
