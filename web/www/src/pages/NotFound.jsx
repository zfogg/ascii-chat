import { NotFound } from "@ascii-chat/shared/components";
import Footer from "../components/Footer";
import { AsciiChatHead } from "../components/AsciiChatHead";
import { SITES } from "@ascii-chat/shared/utils";

export default function NotFoundPage() {
  return (
    <div className="min-h-screen flex flex-col bg-gray-950 text-gray-100">
      <AsciiChatHead
        title="404 - Page Not Found | ascii-chat"
        description="The page you're looking for doesn't exist or has been moved."
        url={`${SITES.MAIN}/404`}
      />
      <div className="flex-1 flex items-center justify-center">
        <NotFound />
      </div>
      <div className="bg-gray-950 mt-8 pt-8">
        <Footer />
      </div>
    </div>
  );
}
