import { NotFound } from "@ascii-chat/shared/components";
import Footer from "../components/Footer";
import { AsciiChatHead } from "../components/AsciiChatHead";
import { SITES } from "@ascii-chat/shared/utils";

export default function NotFoundPage() {
  return (
    <div className="h-full flex flex-col bg-gray-950 text-gray-100">
      <AsciiChatHead
        title="404 - Page Not Found | ascii-chat"
        description="The page you're looking for doesn't exist or has been moved."
        url={`${SITES.MAIN}/404`}
      />
      <NotFound />
      <div className="fixed bottom-0 left-0 right-0 bg-gray-950 mb-8">
        <Footer />
      </div>
    </div>
  );
}
