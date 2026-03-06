import { NotFound } from "@ascii-chat/shared/components";
import { SITES } from "@ascii-chat/shared/utils";
import { WebClientHead } from "../components/WebClientHead";

export function NotFoundPage() {
  return (
    <>
      <WebClientHead
        title="404 - Page Not Found | ascii-chat Web Client"
        description="The page you're looking for doesn't exist or has been moved."
        url={`${SITES.WEB}/404`}
      />
      <NotFound />
    </>
  );
}
