let activeChapter = -1;
const guideKey = (step) =>
  step.symbol ? step.file + "::" + step.symbol : step.file;
function prepareGuideNodes() {
  for (const guide of data.views)
    for (const chapter of guide.chapters)
      for (const step of chapter.steps) {
        if (!step.symbol) continue;
        const base = byId.get(step.file);
        const fn = base?.functions.find((f) => f.name === step.symbol);
        if (!fn) throw new Error("Missing guided reference: " + guideKey(step));
        const id = guideKey(step);
        byId.set(id, {
          ...base,
          id,
          baseId: step.file,
          name: step.symbol + "()",
          functions: [fn],
          lines: fn.end - fn.line + 1,
          description: step.text,
        });
      }
}
function sourceNodeLabel(node) {
  const file = node.baseId || node.id;
  if (file.endsWith(".cpp"))
    return node.kind === "lib" ? "C++ / SHARED LIBRARY" : "NATIVE C++";
  if (file.endsWith(".m")) return "OBJECTIVE-C";
  if (file.endsWith(".js")) return "BROWSER JAVASCRIPT";
  return labels[node.kind];
}
function mappedSourceEdges(ids) {
  const virtual = ids.some((id) => id.includes("::"));
  const pool = virtual
    ? [...data.edges, ...(data.localEdges || [])]
    : data.edges;
  const byFile = new Map();
  for (const id of ids) {
    const file = id.split("::")[0];
    if (!byFile.has(file)) byFile.set(file, []);
    byFile.get(file).push(id);
  }
  const result = [];
  for (const edge of pool) {
    if (!byFile.has(edge.source) || !byFile.has(edge.target)) continue;
    for (const source of byFile.get(edge.source))
      for (const target of byFile.get(edge.target)) {
        if (source === target) continue;
        const a = byId.get(source),
          b = byId.get(target);
        const evidence = edge.evidence.filter((proof) => {
          if (
            a.baseId &&
            (proof.line < a.functions[0].line ||
              proof.line > a.functions[0].end)
          )
            return false;
          if (
            b.baseId &&
            (edge.kind !== "reference" ||
              proof.symbol.replace(/^_/, "") !== b.functions[0].name)
          )
            return false;
          return true;
        });
        if (evidence.length) result.push({ ...edge, source, target, evidence });
      }
  }
  return result;
}
function initializeGuideNavigation() {
  const groups = [...new Set(data.views.map((v) => v.group))];
  $("#path-count").textContent = String(data.views.length).padStart(2, "0");
  $("#views").innerHTML = groups
    .map(
      (group) =>
        `<div class="guide-group">${esc(group)}</div>` +
        data.views
          .filter((v) => v.group === group)
          .map(
            (v) =>
              `<button data-view="${v.id}" title="${esc(v.question)}"><span>${String(data.views.indexOf(v) + 1).padStart(2, "0")}</span><span class="guide-name">${esc(v.title)}</span><span class="guide-arrow">↗</span></button>`,
          )
          .join(""),
    )
    .join("");
  $("#views")
    .querySelectorAll("[data-view]")
    .forEach((el) => (el.onclick = () => showGuide(el.dataset.view)));
}
function showGuide(id, chapterIndex = -1) {
  view = data.views.find((v) => v.id === id) || data.views[0];
  activeChapter =
    chapterIndex >= 0 && chapterIndex < view.chapters.length
      ? chapterIndex
      : -1;
  const chapter = activeChapter < 0 ? null : view.chapters[activeChapter];
  selected = null;
  scopeEdges = null;
  focused = false;
  document.body.classList.remove("inspect-open");
  $("#title").textContent = view.title;
  $("#subtitle").textContent = chapter ? chapter.summary : view.goal;
  $("#path-number").textContent =
    `PATH ${String(data.views.indexOf(view) + 1).padStart(2, "0")} / ${view.group.toUpperCase()}`;
  $("#back").hidden = true;
  $("#guide-notes").hidden = false;
  $("#views")
    .querySelectorAll("[data-view]")
    .forEach((el) =>
      el.classList.toggle("active", el.dataset.view === view.id),
    );
  const tabs = $("#chapter-tabs");
  tabs.hidden = false;
  tabs.innerHTML =
    `<button data-chapter="-1" class="${activeChapter < 0 ? "active" : ""}">Overview</button>` +
    view.chapters
      .map(
        (c, i) =>
          `<button data-chapter="${i}" class="${activeChapter === i ? "active" : ""}"><span>${i + 1}</span>${esc(c.title)}</button>`,
      )
      .join("");
  tabs
    .querySelectorAll("button")
    .forEach(
      (el) => (el.onclick = () => showGuide(view.id, +el.dataset.chapter)),
    );
  for (const c of chapter ? [chapter] : view.chapters)
    for (const step of c.steps) {
      const node = byId.get(guideKey(step));
      if (node.baseId) node.description = step.text;
    }
  $(".canvas-label").textContent =
    (chapter ? "SOURCE CHECKPOINTS · " : "JOURNEY · ") +
    (chapter
      ? chapter.columnTitles.join(" / ")
      : view.columnTitles.join(" → "));
  layout(chapter ? chapter.columns : view.columns);
  readGuide();
  history.replaceState(
    null,
    "",
    "#" + view.id + (chapter ? "/" + chapter.id : ""),
  );
}
function hideGuideContext() {
  $("#chapter-tabs").hidden = true;
  $(".canvas-label").textContent =
    "SOURCE REFERENCES · SELECT A NODE OR CONNECTION";
}
function readGuide() {
  const chapter = activeChapter < 0 ? null : view.chapters[activeChapter];
  $("#inspect-kind").textContent = "GUIDE";
  const steps = chapter ? chapter.steps : [];
  $("#details").innerHTML =
    `<div class="eyebrow accent">${esc(view.scope)}</div><h3 class="guide-question">${esc(chapter ? chapter.title : view.question)}</h3><p>${esc(chapter ? chapter.summary : view.goal)}</p>` +
    (chapter
      ? `<ol class="guide-checkpoints">${steps.map((step, i) => `<li><h4>${esc(step.title)}</h4><p>${esc(step.text)}</p><button class="guide-reference" data-step="${i}"><strong>${esc(step.symbol ? step.symbol + "()" : step.file.split("/").pop())}</strong><small>${esc(step.file)}</small></button></li>`).join("")}</ol><div class="guide-pager"><button id="chapter-prev">← ${activeChapter ? "Previous" : "Overview"}</button>${activeChapter < view.chapters.length - 1 ? '<button id="chapter-next">Next →</button>' : ""}</div>`
      : `<div class="guide-outcome"><h4>YOU WILL BE ABLE TO</h4><p>${esc(view.outcome)}</p></div><h4>FOLLOW THE JOURNEY</h4><div class="guide-outline">${view.chapters.map((c, i) => `<button data-open-chapter="${i}"><span>${String(i + 1).padStart(2, "0")}</span><strong>${esc(c.title)}</strong><p>${esc(c.summary)}</p><small>${c.steps.length} source checkpoints →</small></button>`).join("")}</div>`) +
    `<div class="guide-diagnostic"><h4>IF THIS STAGE FAILS</h4><p>${esc(view.diagnostic)}</p></div><h4>CONTINUE EXPLORING</h4><div class="guide-related">${view.related.map((id) => `<button data-related="${id}">${esc(data.views.find((v) => v.id === id).title)} ↗</button>`).join("")}</div>`;
  $("#details")
    .querySelectorAll("[data-open-chapter]")
    .forEach(
      (el) => (el.onclick = () => showGuide(view.id, +el.dataset.openChapter)),
    );
  $("#details")
    .querySelectorAll("[data-step]")
    .forEach(
      (el) => (el.onclick = () => select(guideKey(steps[+el.dataset.step]))),
    );
  $("#details")
    .querySelectorAll("[data-related]")
    .forEach((el) => (el.onclick = () => showGuide(el.dataset.related)));
  if ($("#chapter-prev"))
    $("#chapter-prev").onclick = () => showGuide(view.id, activeChapter - 1);
  if ($("#chapter-next"))
    $("#chapter-next").onclick = () => showGuide(view.id, activeChapter + 1);
  $(".inspector").scrollTop = 0;
}
