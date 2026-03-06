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
      <NotFound />
      <div className="flex justify-center pb-8">
        <Footer />
      </div>
    </>
  );
}
