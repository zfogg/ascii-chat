import { Heading, Button, Footer } from "@ascii-chat/shared/components";
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
        <header className="text-center mb-12 pb-8 border-b-2 border-gray-700">
          <Heading level={1} className="text-red-500 text-6xl md:text-7xl mb-2">
            404
          </Heading>
          <p className="text-gray-400 text-lg md:text-xl m-0">Page Not Found</p>
          <p className="text-gray-400 text-lg md:text-xl m-0">
            The page you're looking for doesn't exist.
          </p>
        </header>

        <section className="text-center mb-12">
          <Heading
            level={2}
            className="text-blue-400 border-b border-gray-700 pb-2 mb-4 text-2xl md:text-3xl"
          >
            🏠 Go Back
          </Heading>
          <p className="leading-relaxed mb-4">
            <Button
              href="/"
              onClick={() => handleLinkClick("/", "404 back to home")}
            >
              ← Back to Home
            </Button>
          </p>
        </section>

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
      </div>
    </>
  );
}

export default NotFound;
