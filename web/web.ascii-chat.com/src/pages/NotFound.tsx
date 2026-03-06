import { NotFound } from "@ascii-chat/shared/components";
import { WebClientHead } from "../components/WebClientHead";

export function NotFoundPage() {
  return (
    <>
      <WebClientHead
        title="404 - Page Not Found | ascii-chat Web Client"
        description="The page you're looking for doesn't exist or has been moved."
        url="https://web.ascii-chat.com/404"
      />
      <NotFound />
    </>
  );
}
