import { NotFound, Footer } from "@ascii-chat/shared/components";
import { ACDSHead, Header } from "../components";
import { SITES } from "@ascii-chat/shared/utils";

function NotFoundPage() {
  const handleLinkClick = (url: string, text: string) => {
    if (window.gtag) {
      window.gtag("event", "link_click", {
        link_url: url,
        link_text: text,
      });
    }
  };

  return (
    <div className="h-screen flex flex-col bg-gray-950 text-gray-100">
      <ACDSHead
        title="404 - Page Not Found | ascii-chat Discovery Service"
        description="The page you're looking for doesn't exist."
        url={`${SITES.DISCOVERY}/404`}
      />
      <div className="fixed top-0 left-0 right-0 bg-gray-950 z-10">
        <Header />
      </div>
      <div className="flex-1 flex items-center justify-center pt-20 pb-32">
        <NotFound />
      </div>
      <div className="fixed bottom-0 left-0 right-0 bg-gray-950 z-10">
        <Footer
          links={[
            {
              href: "https://github.com/zfogg/ascii-chat",
              label: "📦 GitHub",
              color: "text-cyan-400 hover:text-cyan-300",
              target: "_blank",
              rel: "noopener noreferrer",
              onClick: () =>
                handleLinkClick(
                  "https://github.com/zfogg/ascii-chat",
                  "GitHub (footer 404)",
                ),
            },
            {
              href: "https://zfogg.github.io/ascii-chat/group__module__acds.html",
              label: "📚 ACDS Documentation",
              color: "text-teal-400 hover:text-teal-300",
              target: "_blank",
              rel: "noopener noreferrer",
              onClick: () =>
                handleLinkClick(
                  "https://zfogg.github.io/ascii-chat/group__module__acds.html",
                  "ACDS Documentation (footer 404)",
                ),
            },
            {
              href: "https://github.com/zfogg/ascii-chat/issues",
              label: "🐛 Issues",
              color: "text-purple-400 hover:text-purple-300",
              target: "_blank",
              rel: "noopener noreferrer",
              onClick: () =>
                handleLinkClick(
                  "https://github.com/zfogg/ascii-chat/issues",
                  "Issues (404)",
                ),
            },
            {
              href: "https://github.com/zfogg/ascii-chat/releases",
              label: "📦 Releases",
              color: "text-pink-400 hover:text-pink-300",
              target: "_blank",
              rel: "noopener noreferrer",
              onClick: () =>
                handleLinkClick(
                  "https://github.com/zfogg/ascii-chat/releases",
                  "Releases (404)",
                ),
            },
          ]}
          commitSha={__COMMIT_SHA__}
          onCommitClick={() =>
            handleLinkClick(
              `https://github.com/zfogg/ascii-chat/commit/${__COMMIT_SHA__}`,
              "Commit SHA",
            )
          }
          extraLine="ascii-chat Discovery Service"
        />
      </div>
    </div>
  );
}

export default NotFoundPage;
