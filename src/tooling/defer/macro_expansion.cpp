#include "transform.h"
#include "clang_tokens.h"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace defer_tool {
using namespace detail;

std::string expand_defer_wrappers(CXTranslationUnit unit, const std::string &path, const std::string &source,
                                  bool allow_other_files) {
  // Index all visible definitions: a wrapper can reach defer through other
  // macros, including definitions from headers and conditional redefinitions.
  std::map<std::string, std::vector<CXCursor>> definitions;
  auto root = children(clang_getTranslationUnitCursor(unit));
  for (auto cursor : root)
    if (clang_getCursorKind(cursor) == CXCursor_MacroDefinition)
      definitions[spelling(cursor)].push_back(cursor);
  const std::set<std::string> markers = {"defer", "defer_named", "defer_cancel"};
  std::map<std::string, bool> memo;
  std::set<std::string> visiting;
  // Cycles are not memoized as negative results: a different path through the
  // same cycle may still reach a defer marker.
  std::function<std::pair<bool, bool>(const std::string &)> reaches =
      [&](const std::string &name) -> std::pair<bool, bool> {
    if (markers.count(name))
      return {true, false};
    if (memo.count(name))
      return {memo.at(name), false};
    const auto it = definitions.find(name);
    if (it == definitions.end())
      return {false, false};
    if (visiting.size() >= 128)
      throw std::runtime_error("defer macro dependency nesting exceeds the supported limit");
    if (!visiting.insert(name).second)
      return {false, true};
    bool result = false, cyclic = false;
    for (auto definition : it->second) {
      const auto parts = tokens(unit, clang_getCursorExtent(definition));
      for (std::size_t i = 1; i < parts.size() && !result; ++i)
        if (parts[i].kind == CXToken_Identifier && parts[i].text != name) {
          const auto dependency = reaches(parts[i].text);
          result = dependency.first;
          cyclic = cyclic || dependency.second;
        }
    }
    visiting.erase(name);
    if (result || !cyclic)
      memo[name] = result;
    return {result, cyclic};
  };
  std::vector<Edit> edits;
  const auto canonical = std::filesystem::weakly_canonical(std::filesystem::absolute(std::filesystem::u8path(path)));
  // Only expand invocations whose definition or arguments can contain defer;
  // unrelated macros retain the preprocessor's ordinary behavior.
  for (auto cursor : root) {
    if (clang_getCursorKind(cursor) != CXCursor_MacroExpansion || markers.count(spelling(cursor)))
      continue;
    CXFile file = nullptr;
    clang_getExpansionLocation(clang_getCursorLocation(cursor), &file, nullptr, nullptr, nullptr);
    if (!file)
      continue;
    const auto invocation = tokens(unit, clang_getCursorExtent(cursor));
    bool relevant = reaches(spelling(cursor)).first;
    for (const auto &token : invocation)
      if (token.kind == CXToken_Identifier && markers.count(token.text))
        relevant = true;
    if (!relevant)
      continue;
    unsigned line = 0, column = 0;
    clang_getExpansionLocation(clang_getCursorLocation(cursor), nullptr, &line, &column, nullptr);
    const auto wrapper_error = [&](const std::string &message) {
      return std::runtime_error(consume_string(clang_getFileName(file)) + ":" + std::to_string(line) + ":" +
                                std::to_string(column) + ": " + message);
    };
    for (std::size_t i = 1; i < invocation.size(); ++i) {
      const auto &token = invocation[i];
      if (token.kind == CXToken_Identifier && definitions.count(token.text) && !reaches(token.text).first)
        throw wrapper_error("defer wrapper arguments containing macros cannot be expanded safely; use a local value");
      if (token.text == "__COUNTER__")
        throw wrapper_error("defer wrapper arguments containing __COUNTER__ cannot be expanded safely");
    }
    if (std::filesystem::weakly_canonical(
            std::filesystem::absolute(std::filesystem::u8path(consume_string(clang_getFileName(file))))) != canonical) {
      if (allow_other_files)
        continue;
      throw wrapper_error("defer wrapper in included files is not supported; move the function into a C source file");
    }
    const auto definition = clang_getCursorReferenced(cursor);
    if (clang_Cursor_isNull(definition))
      throw wrapper_error("cannot resolve macro containing defer: " + spelling(cursor));
    const auto parts = tokens(unit, clang_getCursorExtent(definition));
    if (parts.empty())
      throw wrapper_error("cannot read macro containing defer: " + spelling(cursor));
    const bool function = clang_Cursor_isMacroFunctionLike(definition);
    // Restrict expansion to parameter substitution that can be reconstructed
    // without changing preprocessing semantics; reject ambiguous constructs.
    std::size_t body = 1;
    std::vector<std::string> parameters;
    bool variadic = false;
    if (function) {
      if (parts.size() < 3 || parts[1].text != "(")
        throw wrapper_error("cannot parse defer wrapper parameters");
      body = 2;
      while (body < parts.size() && parts[body].text != ")") {
        if (parts[body].text != ",") {
          if (parts[body].text == "..." && body + 1 < parts.size() && parts[body + 1].text == ")") {
            variadic = true;
            parameters.push_back("__VA_ARGS__");
          } else {
            if (parts[body].kind != CXToken_Identifier)
              throw wrapper_error("complex defer wrapper parameters are unsupported");
            parameters.push_back(parts[body].text);
          }
        }
        ++body;
      }
      if (body == parts.size())
        throw wrapper_error("unterminated defer wrapper parameters");
      ++body;
    }
    if (std::any_of(parts.begin() + static_cast<std::ptrdiff_t>(body), parts.end(), [](const Token &part) {
          return part.text == "#" || part.text == "##" || part.text == "__VA_OPT__" || part.text == "_Pragma" ||
                 part.text == "__COUNTER__";
        }))
      throw wrapper_error(
          "defer wrapper uses unsupported stringification, token pasting, __VA_OPT__, _Pragma, or __COUNTER__");
    const auto first = begin(cursor), last = end(cursor);
    if (source.substr(first, last - first).find('\n') != std::string::npos)
      throw wrapper_error("multiline defer wrapper invocations are unsupported");
    std::vector<std::string> arguments;
    if (function) {
      if (invocation.size() < 3 || invocation[1].text != "(")
        throw wrapper_error("cannot parse defer wrapper invocation");
      unsigned argument_start = invocation[1].finish;
      // Commas nested in parentheses, brackets, or braces belong to an argument.
      int depth = 0;
      for (std::size_t i = 2; i < invocation.size(); ++i) {
        const auto &token = invocation[i];
        if (token.text == ")" && depth == 0) {
          const auto value = trim(source.substr(argument_start, token.start - argument_start));
          if (!value.empty() || !parameters.empty())
            arguments.push_back(value);
          break;
        }
        if (token.text == "," && depth == 0) {
          arguments.push_back(trim(source.substr(argument_start, token.start - argument_start)));
          argument_start = token.finish;
        } else if (token.text == "(" || token.text == "[" || token.text == "{")
          ++depth;
        else if (token.text == ")" || token.text == "]" || token.text == "}")
          --depth;
      }
      const auto fixed = parameters.size() - (variadic ? 1U : 0U);
      if ((!variadic && arguments.size() != parameters.size()) || (variadic && arguments.size() < fixed))
        throw wrapper_error("defer wrapper argument count does not match its definition");
      if (variadic) {
        std::string remaining;
        for (std::size_t i = fixed; i < arguments.size(); ++i) {
          if (i > fixed)
            remaining += ", ";
          remaining += arguments[i];
        }
        arguments.resize(fixed);
        arguments.push_back(std::move(remaining));
      }
    }
    std::string replacement;
    for (std::size_t i = body; i < parts.size(); ++i) {
      if (parts[i].kind == CXToken_Comment)
        continue;
      const auto param = std::find(parameters.begin(), parameters.end(), parts[i].text);
      if (!replacement.empty())
        replacement += ' ';
      replacement +=
          param == parameters.end() ? parts[i].text : arguments[static_cast<std::size_t>(param - parameters.begin())];
    }
    edits.push_back({first, last, replacement});
  }
  std::sort(edits.begin(), edits.end(),
            [](const Edit &a, const Edit &b) { return a.start != b.start ? a.start < b.start : a.finish > b.finish; });
  // Expand only the outermost invocation in this pass. A later pass handles
  // nested wrappers after their enclosing macro has been substituted.
  std::vector<Edit> outermost;
  for (const auto &edit : edits) {
    if (!outermost.empty() && edit.start < outermost.back().finish) {
      if (edit.finish > outermost.back().finish)
        throw std::runtime_error("overlapping defer macro expansions cannot be transformed safely");
      continue;
    }
    outermost.push_back(edit);
  }
  auto result = source;
  for (auto edit = outermost.rbegin(); edit != outermost.rend(); ++edit)
    result.replace(edit->start, edit->finish - edit->start, edit->replacement);
  return result;
}

} // namespace defer_tool
