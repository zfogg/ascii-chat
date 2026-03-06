import {
  NotFound as NotFoundComponent,
  Footer,
} from "@ascii-chat/shared/components";
import { ACDSHead } from "../components/ACDSHead";

function NotFound() {
  const handleLinkClick = (url, text) => {
    if (window.gtag) {
      window.gtag("event", "link_click", {
        link_url: url,
        link_text: text,
      });
    }
  };

  return (
    <>
      <ACDSHead
        title="404 - Page Not Found | ascii-chat Discovery Service"
        description="The page you're looking for doesn't exist."
        url="https://discovery.ascii-chat.com/404"
      />
      <div className="max-w-4xl mx-auto px-4 md:px-8 py-8 md:py-16">
        <NotFoundComponent
          headingText="Page Not Found"
          descriptionText="The page you're looking for doesn't exist."
          headingClassName="text-lg md:text-xl text-gray-400 mb-2 font-bold"
          preClassName="text-red-500 text-6xl md:text-7xl mb-2 overflow-hidden"
          descriptionClassName="text-gray-400 text-lg md:text-xl m-0 mb-6"
          buttonClassName="inline-block bg-cyan-600 hover:bg-cyan-500 text-white font-semibold px-6 py-3 rounded transition-colors"
          containerClassName="text-center mb-12 pb-8 border-b-2 border-gray-700"
          footer={
            <Footer
              links={[
                {
                  href: "https://github.com/zfogg/ascii-chat",
                  label: "📦 GitHub",
                  color: "text-cyan-400 hover:text-cyan-300",
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
          }
        />
      </div>
    </>
  );
}

export default NotFound;
