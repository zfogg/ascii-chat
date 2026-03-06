import { NotFound } from "@ascii-chat/shared/components";
import Footer from "../components/Footer";
import { AsciiChatHead } from "../components/AsciiChatHead";

export default function NotFoundPage() {
  return (
    <>
      <AsciiChatHead
        title="404 - Page Not Found | ascii-chat"
        description="The page you're looking for doesn't exist or has been moved."
        url="https://ascii-chat.com/404"
      />
      <div className="bg-gray-950 text-gray-100 flex flex-col items-center pt-[22vh]">
        <div className="max-w-4xl px-4 sm:px-6">
          <NotFound
            headingClassName="text-2xl sm:text-3xl text-gray-300 mb-4 font-bold"
            descriptionClassName="text-base sm:text-lg text-gray-400 mb-6"
            preClassName="text-6xl sm:text-7xl md:text-8xl font-bold mb-4 overflow-hidden text-red-400"
            buttonClassName="inline-block bg-cyan-600 hover:bg-cyan-500 text-white font-semibold px-6 py-3 rounded-lg transition-colors"
            containerClassName="flex items-center justify-center p-4"
            footer={<Footer />}
          />
        </div>
      </div>
    </>
  );
}
