import { Link } from "react-router-dom";
import Footer from "../components/Footer";
import { AsciiChatHead } from "../components/AsciiChatHead";

export default function NotFound() {
  return (
    <>
      <AsciiChatHead
        title="404 - Page Not Found | ascii-chat"
        description="The page you're looking for doesn't exist or has been moved."
        url="https://ascii-chat.com/404"
      />
      <div className="bg-gray-950 text-gray-100 flex flex-col items-center pt-[22vh]">
        <div className="max-w-4xl px-4 sm:px-6 text-center">
          {/* Header */}
          <header className="mb-12 sm:mb-16">
            <h1 className="text-6xl sm:text-7xl md:text-8xl font-bold mb-4">
              <span className="text-red-400">404</span>
            </h1>
            <p className="text-2xl sm:text-3xl text-gray-300 mb-4">
              Page Not Found
            </p>
            <p className="text-base sm:text-lg text-gray-400 mb-6">
              The page you're looking for doesn't exist or has been moved.
            </p>
            <Link
              to="/"
              className="inline-block bg-cyan-600 hover:bg-cyan-500 text-white font-semibold px-6 py-3 rounded-lg transition-colors"
            >
              ← Back to Home
            </Link>
          </header>
          {/* Footer */}
          <Footer />
        </div>
      </div>
    </>
  );
}
