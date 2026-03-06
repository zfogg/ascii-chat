import { Button } from "./Button";

export function NotFound() {
  return (
    <div className="flex-1 flex items-center justify-center px-4 sm:px-6">
      <div className="max-w-4xl text-center">
        <header>
          <h1 className="text-6xl sm:text-7xl md:text-8xl font-bold mb-4">
            <span className="text-red-400">404</span>
          </h1>
          <p className="text-2xl sm:text-3xl text-gray-300 mb-4">
            Page Not Found
          </p>
          <p className="text-base sm:text-lg text-gray-400 mb-6">
            The page you&apos;re looking for doesn&apos;t exist or has been moved.
          </p>
          <Button
            href="/"
            className="inline-block bg-cyan-600 hover:bg-cyan-500 text-white font-semibold px-6 py-3 rounded-lg transition-colors"
          >
            ← Back to Home
          </Button>
        </header>
      </div>
    </div>
  );
}
