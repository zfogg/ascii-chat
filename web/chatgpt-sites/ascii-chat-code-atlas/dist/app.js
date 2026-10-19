const $ = (s) => document.querySelector(s),
  esc = (s) =>
    String(s).replace(
      /[&<>"']/g,
      (c) =>
        ({
          "&": "&amp;",
          "<": "&lt;",
          ">": "&gt;",
          '"': "&quot;",
          "'": "&#39;",
        })[c],
    );
let data,
  byId,
  view,
  selected,
  shown = [],
  connections = [],
  positions = {},
  scale = 1,
  tx = 30,
  ty = 50,
  width = 1500,
  height = 650,
  focused = false,
  scopeEdges = null;
const nodeWidth = parseFloat(
    getComputedStyle(document.documentElement).getPropertyValue("--node-width"),
  ),
  columnStep = nodeWidth + 80;
const colors = { c: "#a4c6fa", ts: "#c7a8fb", wasm: "#edba72", lib: "#77d6b3" },
  labels = {
    c: "NATIVE C",
    ts: "TYPESCRIPT",
    wasm: "C / WASM BRIDGE",
    lib: "SHARED LIBRARY",
  };
function sourceLink(id, line = 1) {
  return `https://github.com/zfogg/ascii-chat/blob/${data.sha}/${id.split("::")[0]}#L${line}`;
}
function transform() {
  $("#world").style.transform = `translate(${tx}px,${ty}px) scale(${scale})`;
  $("#zoom").textContent = Math.round(scale * 100) + "%";
}
function fit() {
  const r = $("#viewport").getBoundingClientRect();
  scale = Math.min(1.15, (r.width - 65) / width, (r.height - 105) / height);
  scale = Math.max(0.18, scale);
  tx = (r.width - width * scale) / 2;
  ty = Math.max(50, (r.height - height * scale) / 2);
  transform();
}
function zoom(f, x, y) {
  const r = $("#viewport").getBoundingClientRect();
  x ??= r.width / 2;
  y ??= r.height / 2;
  const old = scale;
  scale = Math.min(2, Math.max(0.18, scale * f));
  tx = x - ((x - tx) * scale) / old;
  ty = y - ((y - ty) * scale) / old;
  transform();
}
function layout(columns) {
  positions = {};
  width = columns.length * columnStep - 60;
  height = Math.max(...columns.map((c) => c.length)) * 135 + 100;
  columns.forEach((col, i) =>
    col.forEach(
      (id, j) =>
        (positions[id] = {
          x: i * columnStep,
          y: 100 + j * 135 + (height - 100 - col.length * 135) / 2,
          col: i,
        }),
    ),
  );
  shown = columns.flat();
  connections = scopeEdges
    ? scopeEdges.filter(
        (e) => shown.includes(e.source) && shown.includes(e.target),
      )
    : mappedSourceEdges(shown);
  render();
  fit();
}
function render() {
  $("#world").style.width = width + "px";
  $("#world").style.height = height + "px";
  $("#nodes").innerHTML = shown
    .map((id) => {
      const n = byId.get(id),
        p = positions[id];
      return `<button class="node ${n.kind}" data-id="${esc(id)}" style="left:${p.x}px;top:${p.y}px" title="${esc(n.description)}"><small>${sourceNodeLabel(n)}</small><strong>${esc(n.name)}</strong><span class="path">${esc(id.slice(0, id.lastIndexOf("/")))}</span></button>`;
    })
    .join("");
  const svg = $("#edges");
  svg.setAttribute("width", width);
  svg.setAttribute("height", height);
  let longCount = 0;
  svg.innerHTML =
    '<defs><marker id="arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M 0 0 L 10 5 L 0 10 z" fill="#8cafa7"/></marker></defs>' +
    connections
      .map((e, i) => {
        const a = positions[e.source],
          b = positions[e.target],
          forward = b.col > a.col;
        const outs = connections.filter((x) => x.source === e.source),
          ins = connections.filter((x) => x.target === e.target);
        let x1 = a.x + (forward ? nodeWidth : 0),
          x2 = b.x + (forward ? 0 : nodeWidth),
          y1 = a.y + 30 + ((outs.indexOf(e) + 1) * 52) / (outs.length + 1),
          y2 = b.y + 30 + ((ins.indexOf(e) + 1) * 52) / (ins.length + 1),
          d;
        if (Math.abs(a.col - b.col) === 1) {
          const gap = (x1 + x2) / 2 + ((i % 5) - 2) * 6;
          d = `M${x1},${y1} H${gap} V${y2} H${x2}`;
        } else {
          const lane = 10 + (longCount++ % 10) * 7;
          d = `M${x1},${y1} H${x1 + (forward ? 20 : -20)} V${lane} H${x2 + (forward ? -20 : 20)} V${y2} H${x2}`;
        }
        return `<g data-edge="${i}" tabindex="0" role="button" aria-label="${esc(e.source + " to " + e.target)}"><path class="wire ${e.kind}" d="${d}" marker-end="url(#arrow)"/><path class="hit" d="${d}"><title>${esc(e.source + " → " + e.target + " (" + e.kind + ")")}</title></path></g>`;
      })
      .join("");
  $("#counts").textContent =
    `${shown.length} source nodes / ${connections.length} connections`;
  $("#empty").hidden = connections.length > 0;
  $("#nodes")
    .querySelectorAll(".node")
    .forEach((el) => {
      el.onclick = () => select(el.dataset.id);
      el.onmouseenter = () => {
        const n = byId.get(el.dataset.id),
          r = el.getBoundingClientRect(),
          tip = $("#tooltip");
        tip.innerHTML =
          "<strong>" +
          esc(n.id) +
          "</strong><p>" +
          esc(n.description) +
          "</p><small>" +
          n.functions.length +
          " symbols · Click to inspect</small>";
        tip.hidden = false;
        tip.style.left =
          Math.min(window.innerWidth - 300, Math.max(12, r.left)) + "px";
        tip.style.top = Math.max(80, r.top - 120) + "px";
      };
      el.onmouseleave = () => ($("#tooltip").hidden = true);
    });
  svg.querySelectorAll("[data-edge]").forEach((el) => {
    el.onclick = () =>
      inspectEdge(connections[+el.dataset.edge], +el.dataset.edge);
    el.onkeydown = (e) => {
      if (e.key === "Enter" || e.key === " ") {
        e.preventDefault();
        el.onclick();
      }
    };
  });
  if (selected && shown.includes(selected)) highlight(selected);
}
function highlight(id) {
  const connected = new Set([
    id,
    ...connections
      .filter((e) => e.source === id || e.target === id)
      .flatMap((e) => [e.source, e.target]),
  ]);
  $("#nodes")
    .querySelectorAll(".node")
    .forEach((el) => {
      el.classList.toggle("selected", el.dataset.id === id);
      el.classList.toggle("dim", !connected.has(el.dataset.id));
    });
  $("#edges")
    .querySelectorAll("[data-edge]")
    .forEach((el) => {
      const e = connections[+el.dataset.edge];
      el.querySelector(".wire").classList.toggle(
        "dim",
        e.source !== id && e.target !== id,
      );
      el.querySelector(".wire").classList.remove("selected");
    });
}
function showView(id) {
  showGuide(id);
}
function select(id) {
  if (document.body.classList.contains("expanded"))
    document.body.classList.add("inspect-open");
  selected = id;
  const n = byId.get(id);
  if (!n) return;
  highlight(id);
  if (n.baseId) {
    showFunctionReference(n.baseId, n.functions[0]);
    return;
  }
  $("#inspect-kind").textContent = "FILE";
  const incoming = (scopeEdges || data.edges).filter((e) => e.target === id),
    outgoing = (scopeEdges || data.edges).filter((e) => e.source === id);
  $("#details").innerHTML =
    `<div class="eyebrow accent">${sourceNodeLabel(n)}</div><h3>${esc(n.name)}</h3><div class="filepath">${esc(id)}</div><p>${esc(n.description)}</p><div class="file-stats"><span class="pill">${n.lines.toLocaleString()} lines</span><span class="pill">${n.functions.length} indexed symbols</span></div><div class="actions"><button id="explore">Explore file →</button><a target="_blank" rel="noopener" href="${sourceLink(id)}">Source ↗</a></div><h4>INDEXED SYMBOLS</h4><div id="symbols">${n.functions.length ? n.functions.map((f, i) => `<button class="symbol" data-fn="${i}">${esc(f.name)}<span>L${f.line}</span></button>`).join("") : "<p>No top-level symbols indexed.</p>"}</div><div id="code"></div><h4>DEPENDENCIES <span> / ${outgoing.length}</span></h4>${outgoing.map((e) => `<button class="neighbor" data-target="${esc(e.target)}">→ ${esc(e.target)}</button>`).join("") || "<p>No indexed outgoing relationships.</p>"}<h4>REFERENCED BY / ${incoming.length}</h4>${incoming.map((e) => `<button class="neighbor" data-target="${esc(e.source)}">← ${esc(e.source)}</button>`).join("")}`;
  $("#explore").onclick = () => explore(id.split("::")[0]);
  $("#symbols")
    .querySelectorAll("button")
    .forEach(
      (el) =>
        (el.onclick = () =>
          showFunctionReference(id, n.functions[+el.dataset.fn])),
    );
  $("#details")
    .querySelectorAll("[data-target]")
    .forEach(
      (el) =>
        (el.onclick = () => {
          const target = el.dataset.target;
          if (shown.includes(target)) select(target);
          else explore(target);
        }),
    );
}
function showFunctionReference(id, f) {
  const file = id.split("::")[0];
  $("#inspect-kind").textContent = "FUNCTION";
  $("#details").innerHTML =
    `<button id="reference-back">← File</button><h3>${esc(f.name)}</h3><div class="filepath">${esc(file)}:${f.line}–${f.end}</div><div id="code">${f.snippetHtml || `<pre class="snippet">${esc(f.snippet)}</pre>`}</div><a href="${sourceLink(file, f.line)}" target="_blank" rel="noopener">Source ↗</a>${file.endsWith(".c") ? '<button id="reference-trace">Trace function →</button>' : ""}`;
  $("#reference-back").onclick = () => select(file);
  if ($("#reference-trace"))
    $("#reference-trace").onclick = () => traceFunction(file, f);
  $(".inspector").scrollTop = 0;
}

function explore(id) {
  hideGuideContext();
  scopeEdges = null;
  focused = true;
  $("#back").hidden = false;
  const incoming = [
      ...new Set(
        data.edges.filter((e) => e.target === id).map((e) => e.source),
      ),
    ],
    outgoing = [
      ...new Set(
        data.edges.filter((e) => e.source === id).map((e) => e.target),
      ),
    ].filter((x) => !incoming.includes(x));
  $("#title").textContent = byId.get(id).name;
  $("#subtitle").textContent =
    "Immediate source neighbors. Select a file to continue exploring; each connection includes source evidence.";
  $("#path-number").textContent = "FILE EXPLORER / ONE HOP";
  layout([incoming, [id], outgoing]);
  select(id);
  history.replaceState(null, "", "#file=" + encodeURIComponent(id));
}

function traceFunction(id, f) {
  hideGuideContext();
  focused = true;
  const n = byId.get(id),
    virtual = id + "::" + f.name;
  byId.set(virtual, {
    ...n,
    id: virtual,
    name: f.name + "()",
    functions: [f],
    lines: f.end - f.line + 1,
    description:
      "Function scope in " +
      id +
      ". Incoming arrows reference this function; outgoing arrows reference symbols within its body. Lexical analysis does not resolve callback execution.",
  });
  const incoming = data.edges
    .filter(
      (e) =>
        e.target === id &&
        e.evidence.some((x) => x.symbol.replace(/^_/, "") === f.name),
    )
    .map((e) => ({
      ...e,
      target: virtual,
      evidence: e.evidence.filter((x) => x.symbol.replace(/^_/, "") === f.name),
    }));
  const outgoing = data.edges
    .filter(
      (e) =>
        e.source === id &&
        e.evidence.some((x) => x.line >= f.line && x.line <= f.end),
    )
    .map((e) => ({
      ...e,
      source: virtual,
      evidence: e.evidence.filter((x) => x.line >= f.line && x.line <= f.end),
    }));
  scopeEdges = [...incoming, ...outgoing];
  const left = [...new Set(incoming.map((e) => e.source))],
    right = [...new Set(outgoing.map((e) => e.target))].filter(
      (x) => !left.includes(x),
    );
  $("#back").hidden = false;
  $("#title").textContent = f.name + "()";
  $("#path-number").textContent = "FUNCTION TRACE / C";
  $("#subtitle").textContent =
    id +
    " · lines " +
    f.line +
    "–" +
    f.end +
    " · source references within this function scope";
  layout([left, [virtual], right]);
  select(virtual);
}

function inspectEdge(e, i) {
  if (document.body.classList.contains("expanded"))
    document.body.classList.add("inspect-open");
  $("#inspect-kind").textContent = e.kind.toUpperCase();
  $("#edges")
    .querySelectorAll(".wire")
    .forEach((el) => el.classList.remove("selected"));
  $("#edges")
    .querySelector(`[data-edge="${i}"] .wire`)
    .classList.add("selected");
  $("#details").innerHTML =
    `<div class="eyebrow accent">SOURCE EVIDENCE</div><h3>${e.kind === "import" ? "Module import" : e.kind === "resource" ? "Module resource" : "Symbol reference"}</h3><p class="filepath">${esc(e.source)}<br>↓<br>${esc(e.target)}</p><p>${e.kind === "resource" ? "The source constructs a URL for this local module or worker resource. URL construction alone is not execution." : e.kind === "import" ? "The source imports this relative module." : "The source references an indexed symbol through call syntax or a callback registration. This is lexical evidence; build variants and runtime dispatch can change execution."}</p>${e.evidence.map((x) => `<div class="evidence"><code>${esc(x.symbol)}</code>${x.html || `<pre class="snippet">${esc(x.text)}</pre>`}<a href="${sourceLink(e.source, x.line)}" target="_blank" rel="noopener">${esc(byId.get(e.source).name)}:${x.line} ↗</a></div>`).join("")}<button id="edge-source">Inspect source</button> <button id="edge-target">Inspect target</button>`;
  $("#edge-source").onclick = () => select(e.source);
  $("#edge-target").onclick = () => select(e.target);
}
async function init() {
  data = $("#source-data")
    ? JSON.parse($("#source-data").textContent)
    : await (await fetch("data.json")).json();
  byId = new Map(data.nodes.map((n) => [n.id, n]));
  prepareGuideNodes();
  $("#snapshot").innerHTML =
    `SOURCE SNAPSHOT · OCT 18, 2026<br>${data.sha.slice(0, 12)} · working tree<br>${data.nodes.length} files · ${data.nodes.reduce((a, n) => a + n.functions.length, 0).toLocaleString()} symbols`;
  $("#method-note").textContent = data.note;
  initializeGuideNavigation();
  const hash = decodeURIComponent(location.hash.slice(1));
  if (hash.startsWith("file=") && byId.has(hash.slice(5))) {
    view = data.views[0];
    explore(hash.slice(5));
  } else {
    const [id, chapter] = hash.split("/");
    const guide = data.views.find((v) => v.id === id);
    showGuide(id, guide?.chapters.findIndex((c) => c.id === chapter) ?? -1);
  }
}
const tooltip = document.createElement("div");
tooltip.id = "tooltip";
tooltip.hidden = true;
document.body.appendChild(tooltip);
$("#expand").onclick = () => {
  document.body.classList.toggle("expanded");
  document.body.classList.remove("inspect-open");
  fit();
};
$("#detail-close").onclick = () =>
  document.body.classList.remove("inspect-open");
$("#guide-notes").onclick = () => {
  if (focused) showGuide(view.id, activeChapter);
  else readGuide();
  document.body.classList.add("inspect-open");
  $(".inspector").focus({ preventScroll: true });
};
$("#plus").onclick = () => zoom(1.2);
$("#minus").onclick = () => zoom(1 / 1.2);
$("#fit").onclick = fit;
$("#reset").onclick = () => showView(view.id);
$("#back").onclick = () => showGuide(view.id, activeChapter);
$("#about").onclick = $("#method").onclick = () => $("#modal").showModal();
$("#close-modal").onclick = () => $("#modal").close();
$("#modal").onclick = (e) => {
  if (e.target === $("#modal")) $("#modal").close();
};
$("#search").oninput = () => {
  const q = $("#search").value.trim().toLowerCase(),
    res = $("#results");
  res.hidden = !q;
  if (!q) return;
  let matches = data.nodes
    .filter(
      (n) =>
        n.id.toLowerCase().includes(q) ||
        n.functions.some((f) => f.name.toLowerCase().includes(q)),
    )
    .sort((a, b) => (a.kind === "lib") - (b.kind === "lib"))
    .slice(0, 35);
  const guides = data.views
    .filter((v) =>
      (v.title + " " + v.question + " " + v.goal).toLowerCase().includes(q),
    )
    .slice(0, 6);
  res.innerHTML =
    guides
      .map(
        (v) =>
          `<button data-guide="${v.id}">${esc(v.title)}<small>GUIDED PATH · ${esc(v.scope)}</small></button>`,
      )
      .join("") +
      matches
        .map((n) => {
          let f = n.functions.find((f) => f.name.toLowerCase().includes(q));
          return `<button data-id="${esc(n.id)}">${esc(n.name)}<small>${esc(n.id)}</small>${f ? `<small>ƒ ${esc(f.name)}</small>` : ""}</button>`;
        })
        .join("") || '<p style="padding:12px">No source matches.</p>';
  res.querySelectorAll("button").forEach(
    (el) =>
      (el.onclick = () => {
        if (el.dataset.guide) showGuide(el.dataset.guide);
        else explore(el.dataset.id);
        res.hidden = true;
        $("#search").value = "";
      }),
  );
};
document.addEventListener("keydown", (e) => {
  if (e.key === "/" && document.activeElement.tagName !== "INPUT") {
    e.preventDefault();
    $("#search").focus();
  }
  if (e.key === "Escape") {
    $("#results").hidden = true;
    $("#search").blur();
  }
  if (document.activeElement === $("#viewport")) {
    if (e.key === "+") zoom(1.2);
    if (e.key === "-") zoom(1 / 1.2);
    if (e.key === "0") fit();
  }
});
let drag = null;
$("#viewport").addEventListener("pointerdown", (e) => {
  if (e.target.closest("button,[data-edge]")) return;
  drag = { x: e.clientX, y: e.clientY, tx, ty };
  $("#viewport").setPointerCapture(e.pointerId);
});
$("#viewport").addEventListener("pointermove", (e) => {
  if (!drag) return;
  tx = drag.tx + e.clientX - drag.x;
  ty = drag.ty + e.clientY - drag.y;
  transform();
});
$("#viewport").addEventListener("pointerup", () => (drag = null));
$("#viewport").addEventListener("pointercancel", () => (drag = null));
$("#viewport").addEventListener(
  "wheel",
  (e) => {
    e.preventDefault();
    const r = $("#viewport").getBoundingClientRect();
    zoom(Math.exp(-e.deltaY * 0.0015), e.clientX - r.left, e.clientY - r.top);
  },
  { passive: false },
);
window.addEventListener("resize", fit);
init().catch((e) => {
  $("#details").textContent = "Could not load source data: " + e.message;
});
