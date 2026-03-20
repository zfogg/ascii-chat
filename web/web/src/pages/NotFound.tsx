import { NotFound } from "@ascii-chat/shared/components";
import { SITES } from "@ascii-chat/shared/utils";
import { AsciiChatWebHead } from "../components";

export function NotFoundPage() {
  return (
    <div className="flex items-center justify-center flex-1 bg-gray-950 text-gray-100">
      <AsciiChatWebHead
        title="404 - Page Not Found | ascii-chat Web Client"
        description="The page you're looking for doesn't exist or has been moved."
        url={`${SITES.WEB}/404`}
      />
      <NotFound />
    </div>
  );
}
