import { NotFound } from "@ascii-chat/shared/components";
import { Footer, AsciiChatHead } from "../components";
import { SITES } from "@ascii-chat/shared/utils";
import { pageMetadata } from "../metadata";

export default function NotFoundPage() {
  return (
    <div className="flex flex-col flex-1 bg-gray-950 text-gray-100">
      <AsciiChatHead
        title={pageMetadata.notFound.title}
        description={pageMetadata.notFound.description}
        url={`${SITES.MAIN}/404`}
      />
      <div className="flex-1 flex items-center justify-center">
        <NotFound />
      </div>
      <div className="bg-gray-950 mt-8 pt-8 pb-4">
        <Footer />
      </div>
    </div>
  );
}
