import ts from "typescript";
import console from "node:console";
import { URL } from "node:url";
import { posix } from "node:path";
import { readFile, writeFile } from "node:fs/promises";
const file = new URL("./dist/data.json", import.meta.url);
const root = new URL("../../../", import.meta.url);
const data = JSON.parse(await readFile(file, "utf8"));
const byFile = new Map(data.nodes.map((node) => [node.id, node]));
let functions = 0,
  references = 0;
for (const node of data.nodes.filter((n) => /\.(?:tsx?|js)$/.test(n.id))) {
  const text = (await readFile(new URL(node.id, root), "utf8")).replace(
    /\r\n/g,
    "\n",
  );
  const source = ts.createSourceFile(
    node.id,
    text,
    ts.ScriptTarget.Latest,
    true,
    node.id.endsWith(".tsx")
      ? ts.ScriptKind.TSX
      : node.id.endsWith(".js")
        ? ts.ScriptKind.JS
        : ts.ScriptKind.TS,
  );
  const declarations = [],
    calls = [],
    imports = [];
  const lineOf = (offset) =>
    source.getLineAndCharacterOfPosition(offset).line + 1;
  function visit(item) {
    if (
      (ts.isFunctionDeclaration(item) ||
        ts.isFunctionExpression(item) ||
        ts.isClassDeclaration(item) ||
        ts.isVariableDeclaration(item)) &&
      item.name &&
      ts.isIdentifier(item.name)
    )
      declarations.push(item);
    if (
      ts.isCallExpression(item) ||
      ts.isNewExpression(item) ||
      ts.isMethodSignature(item)
    )
      calls.push(item);
    if (ts.isImportDeclaration(item)) imports.push(item);
    ts.forEachChild(item, visit);
  }
  visit(source);
  // Conditional constructor selection still references concrete imported classes.
  const importedClasses = new Map();
  for (const declaration of imports) {
    const specifier = declaration.moduleSpecifier.text;
    if (
      !specifier.startsWith(".") ||
      !declaration.importClause?.namedBindings ||
      !ts.isNamedImports(declaration.importClause.namedBindings)
    )
      continue;
    const base = posix.normalize(posix.join(posix.dirname(node.id), specifier));
    const target = [
      base,
      base + ".ts",
      base + ".tsx",
      base + "/index.ts",
      base + "/index.tsx",
    ].find((path) => byFile.has(path));
    if (!target) continue;
    for (const binding of declaration.importClause.namedBindings.elements) {
      const name = binding.propertyName?.text || binding.name.text;
      if (
        byFile
          .get(target)
          .functions.some((f) => f.name === name && /\bclass\s/.test(f.snippet))
      )
        importedClasses.set(binding.name.text, { target, name });
    }
  }
  for (const call of calls.filter(
    (call) => ts.isNewExpression(call) && !ts.isIdentifier(call.expression),
  )) {
    const used = new Map();
    function inspect(expression) {
      if (ts.isIdentifier(expression) && importedClasses.has(expression.text))
        used.set(expression.text, importedClasses.get(expression.text));
      ts.forEachChild(expression, inspect);
    }
    inspect(call.expression);
    for (const { target, name } of used.values()) {
      let edge = data.edges.find(
        (edge) => edge.source === node.id && edge.target === target,
      );
      if (!edge) {
        edge = { source: node.id, target, kind: "reference", evidence: [] };
        data.edges.push(edge);
      }
      if (edge.kind === "import") {
        edge.kind = "reference";
        edge.evidence = edge.evidence.filter(
          (proof) => !proof.symbol.startsWith("."),
        );
      }
      const line = lineOf(call.getStart(source));
      if (
        !edge.evidence.some(
          (proof) => proof.symbol === name && proof.line === line,
        )
      )
        edge.evidence.push({ symbol: name, line, text: call.getText(source) });
    }
  }
  const completeSymbols = [];
  for (const symbol of node.functions) {
    const matches = declarations.filter((d) => d.name.text === symbol.name);
    const declaration = matches.sort(
      (a, b) =>
        Math.abs(lineOf(a.getStart(source)) - symbol.line) -
        Math.abs(lineOf(b.getStart(source)) - symbol.line),
    )[0];
    if (!declaration) continue;
    let selected = declaration;
    if (
      ts.isVariableDeclaration(declaration) &&
      ts.isVariableDeclarationList(declaration.parent) &&
      declaration.parent.declarations.length === 1
    )
      selected = declaration.parent.parent;
    symbol.line = lineOf(selected.getStart(source));
    symbol.end = lineOf(selected.getEnd());
    symbol.snippet = selected.getText(source);
    completeSymbols.push(symbol);
    functions++;
  }
  node.functions = completeSymbols;
  for (const edge of data.edges.filter((e) => e.source === node.id))
    for (const proof of edge.evidence) {
      if (edge.kind === "import") {
        const declaration = imports.find(
          (d) => d.moduleSpecifier.text === proof.symbol,
        );
        if (declaration) {
          proof.text = declaration.getText(source);
          proof.line = lineOf(declaration.getStart(source));
          references++;
        }
        continue;
      }
      const candidates = calls
        .filter((call) => {
          const expr = ts.isMethodSignature(call) ? call.name : call.expression;
          const name = ts.isPropertyAccessExpression(expr)
            ? expr.name.text
            : ts.isIdentifier(expr)
              ? expr.text
              : "";
          return (
            name === proof.symbol &&
            lineOf(call.getStart(source)) <= proof.line &&
            lineOf(call.getEnd()) >= proof.line
          );
        })
        .sort((a, b) => a.end - a.pos - (b.end - b.pos));
      if (candidates.length) {
        proof.text = candidates[0].getText(source);
        references++;
      }
    }
}
await writeFile(file, JSON.stringify(data));
console.log(
  `Extracted ${functions} complete TS/TSX declarations and ${references} exact references using the TypeScript parser.`,
);
