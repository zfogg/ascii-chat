import { NotFound } from "@ascii-chat/shared/components";
import { WebClientHead } from "../components/WebClientHead";

export function NotFoundPage() {
  return (
    <div className="min-h-screen flex flex-col bg-gray-950 text-gray-100">
      <WebClientHead
        title="404 - Page Not Found | ascii-chat Web Client"
        description="The page you're looking for doesn't exist or has been moved."
        url="https://web.ascii-chat.com/404"
      />
      <NotFound />
    </div>
  );
}
