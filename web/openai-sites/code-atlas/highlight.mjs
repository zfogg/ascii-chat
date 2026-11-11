import { readFile, writeFile } from "node:fs/promises";
import console from "node:console";
import { URL } from "node:url";
import { createHash } from "node:crypto";
import { createHighlighter } from "shiki";

// The atlas is explicitly dark, regardless of the operating system preference.
const theme = "github-dark";
const highlighter = await createHighlighter({
  themes: [theme],
  langs: ["c", "cpp", "objective-c", "javascript", "typescript", "tsx"],
});
const path = new URL("./dist/data.json", import.meta.url);
const data = JSON.parse(await readFile(path, "utf8"));
const languageFor = (id) =>
  id.split("::")[0].endsWith(".tsx")
    ? "tsx"
    : id.split("::")[0].endsWith(".ts")
      ? "typescript"
      : id.endsWith(".cpp")
        ? "cpp"
        : id.endsWith(".m")
          ? "objective-c"
          : id.endsWith(".js")
            ? "javascript"
            : "c";
const cache = new Map();
function highlight(text, id) {
  const lang = languageFor(id);
  const key = lang + "\0" + text;
  if (!cache.has(key)) {
    cache.set(
      key,
      highlighter
        .codeToHtml(text, { lang, theme })
        .replace('class="shiki ', 'data-theme="dark" class="snippet shiki '),
    );
  }
  return cache.get(key);
}
let count = 0;
for (const node of data.nodes) {
  for (const symbol of node.functions) {
    const hash = createHash("sha256")
      .update(theme + "\0" + languageFor(node.id) + "\0" + symbol.snippet)
      .digest("hex");
    if (symbol.highlightHash !== hash || !symbol.snippetHtml)
      symbol.snippetHtml = highlight(symbol.snippet, node.id);
    symbol.highlightHash = hash;
    count++;
  }
}
for (const edge of [...data.edges, ...(data.localEdges || [])]) {
  for (const evidence of edge.evidence) {
    const hash = createHash("sha256")
      .update(theme + "\0" + languageFor(edge.source) + "\0" + evidence.text)
      .digest("hex");
    if (evidence.highlightHash !== hash || !evidence.html)
      evidence.html = highlight(evidence.text, edge.source);
    evidence.highlightHash = hash;
    count++;
  }
}
data.highlighting = {
  engine: "shiki",
  theme,
  colorScheme: "dark",
  languages: ["c", "cpp", "objective-c", "javascript", "typescript", "tsx"],
};
await writeFile(path, JSON.stringify(data));
highlighter.dispose();
console.log(
  `Highlighted ${count} excerpts with Shiki, theme: ${theme} (dark).`,
);
