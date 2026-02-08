import { useState, useEffect } from "react";
import Footer from "../components/Footer";
import { setBreadcrumbSchema } from "../utils/breadcrumbs";
import { useAnchorNavigation } from "../hooks/useAnchorNavigation";
import { AsciiChatHead } from "../components/AsciiChatHead";

export default function Man5() {
  useEffect(() => {
    setBreadcrumbSchema([
      { name: "Home", path: "/" },
      { name: "Man Page", path: "/man5" },
    ]);
  }, []);
  const [manHtml, setManHtml] = useState("");

  useEffect(() => {
    fetch("/ascii-chat-man5.html")
      .then((r) => r.text())
      .then((html) => {
        // Extract just the body content
        const bodyMatch = html.match(/<body[^>]*>([\s\S]*)<\/body>/i);
        if (bodyMatch) {
          setManHtml(bodyMatch[1]);
        } else {
          setManHtml(html);
        }
      })
      .catch((e) => console.error("Failed to load man page:", e));
  }, []);

  useAnchorNavigation(manHtml);

  return (
    <>
      <AsciiChatHead
        title="ascii-chat(5) - File Formats | ascii-chat"
        description="File formats and configuration reference for ascii-chat. Configuration files, color schemes, and format specifications."
        url="https://ascii-chat.com/man5"
      />
      <div className="bg-gray-950 text-gray-100 flex flex-col">
        <div className="flex-1 flex flex-col max-w-5xl mx-auto px-4 sm:px-6 py-8 sm:py-12 w-full">
          {/* Header */}
          <header className="mb-8 sm:mb-12">
            <h1 className="text-3xl sm:text-4xl md:text-5xl font-bold mb-4">
              <span className="text-cyan-400">📋</span> File Formats
            </h1>
            <p className="text-lg sm:text-xl text-gray-300">
              File format specifications for configuration, authentication, and
              data files
            </p>
          </header>

          {/* Man page content */}
          <div
            className="man-page-content"
            dangerouslySetInnerHTML={{ __html: manHtml }}
          />

          {/* Footer */}
          <div className="mt-16">
            <Footer />
          </div>
        </div>
      </div>
    </>
  );
}
