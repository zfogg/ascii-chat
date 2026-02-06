const fs = require("fs");

// Fix Home.jsx - already done manually, skip

// Fix Crypto.jsx
let crypto = fs.readFileSync("src/pages/Crypto.jsx", "utf8");

// Replace all code blocks with proper template literals
crypto = crypto.replace(
  /<pre className="bg-gray-900 border border-gray-800 rounded-lg p-4 overflow-x-auto">\s*<code className="text-teal-300">([^<]*(?:<[^>]+>[^<]*)*)<\/code>\s*<\/pre>/g,
  (match, content) => {
    // Remove leading whitespace from each line
    const cleaned = content
      .split("\n")
      .map((line) => line.trim())
      .filter((line) => line)
      .join("\n");
    return `<pre className="bg-gray-900 border border-gray-800 rounded-lg p-4 overflow-x-auto"><code className="text-teal-300">{\`${cleaned}\`}</code></pre>`;
  },
);

fs.writeFileSync("src/pages/Crypto.jsx.new", crypto);
console.log("Fixed Crypto.jsx");
