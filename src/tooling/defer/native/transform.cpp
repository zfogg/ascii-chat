#include "transform.h"
#include "clang_support.h"
#include "text.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace defer_tool {
namespace {

unsigned offset(CXSourceLocation location) {
  unsigned result = 0;
  clang_getExpansionLocation(location, nullptr, nullptr, nullptr, &result);
  return result;
}
unsigned begin(CXCursor cursor) {
  return offset(clang_getRangeStart(clang_getCursorExtent(cursor)));
}
unsigned end(CXCursor cursor) {
  return offset(clang_getRangeEnd(clang_getCursorExtent(cursor)));
}
std::string spelling(CXCursor cursor) {
  return consume_string(clang_getCursorSpelling(cursor));
}

struct Token {
  std::string text;
  CXTokenKind kind;
  unsigned start;
  unsigned finish;
};

std::vector<Token> tokens(CXTranslationUnit unit, CXSourceRange extent) {
  CXToken *raw = nullptr;
  unsigned count = 0;
  clang_tokenize(unit, extent, &raw, &count);
  std::vector<Token> result;
  for (unsigned i = 0; i < count; ++i) {
    auto range = clang_getTokenExtent(unit, raw[i]);
    result.push_back({consume_string(clang_getTokenSpelling(unit, raw[i])), clang_getTokenKind(raw[i]),
                      offset(clang_getRangeStart(range)), offset(clang_getRangeEnd(range))});
  }
  clang_disposeTokens(unit, raw, count);
  return result;
}

std::string trim(std::string value) {
  auto whitespace = [](unsigned char c) { return std::isspace(c) != 0; };
  value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), whitespace));
  value.erase(std::find_if_not(value.rbegin(), value.rend(), whitespace).base(), value.end());
  return value;
}

struct Action {
  unsigned start;
  unsigned action_start;
  std::string flag;
  std::string text;
  std::set<std::string> identifiers;
};
struct Scope {
  unsigned start;
  std::vector<Action> actions;
};
struct Edit {
  unsigned start;
  unsigned finish;
  std::string replacement;
};
struct Declaration {
  std::string name;
  unsigned position;
  std::vector<unsigned> scopes;
  unsigned limit;
};
struct Directive {
  unsigned position;
  std::string kind;
  std::string name;
};

bool prefix(const std::vector<unsigned> &whole, const std::vector<unsigned> &part) {
  return whole.size() >= part.size() && std::equal(part.begin(), part.end(), whole.begin());
}

class Transformer {
public:
  Transformer(CXTranslationUnit unit, std::string path, std::string source)
      : unit_(unit), path_(std::filesystem::weakly_canonical(std::filesystem::absolute(std::filesystem::u8path(path)))
                               .generic_u8string()),
        source_(std::move(source)) {
    if (source_.find("__ascii_defer_") != std::string::npos)
      fail("reserved identifier prefix __ascii_defer_ occurs in input");
    const std::regex directive(
        R"((^|\n)[ \t]*#[ \t]*(define|undef|include|include_next|import|pragma)\b[ \t]*([A-Za-z_][A-Za-z_0-9]*)?)");
    for (auto it = std::sregex_iterator(source_.begin(), source_.end(), directive); it != std::sregex_iterator(); ++it)
      directives_.push_back({static_cast<unsigned>(it->position()), (*it)[2], (*it)[3]});
    for (auto cursor : children(clang_getTranslationUnitCursor(unit_))) {
      const auto kind = clang_getCursorKind(cursor);
      if (kind == CXCursor_MacroDefinition)
        macro_definitions_[spelling(cursor)].push_back(cursor);
      if (kind == CXCursor_MacroExpansion && spelling(cursor) == "defer" && in_file(cursor))
        macros_.emplace(begin(cursor), cursor);
    }
    if (!macros_.empty() && std::regex_search(source_, std::regex(R"((^|\n)[ \t]*#[ \t]*(line\b|[0-9]))")))
      fail("source-level line directives cannot be preserved safely");
  }

  std::string run();

private:
  [[noreturn]] static void fail(const std::string &message) {
    throw std::runtime_error(message);
  }
  bool in_file(CXCursor cursor) const {
    CXFile file = nullptr;
    clang_getExpansionLocation(clang_getCursorLocation(cursor), &file, nullptr, nullptr, nullptr);
    if (!file)
      return false;
    const auto file_path = std::filesystem::weakly_canonical(
        std::filesystem::absolute(std::filesystem::u8path(consume_string(clang_getFileName(file)))));
    std::error_code error;
    const bool equivalent = std::filesystem::equivalent(file_path, std::filesystem::u8path(path_), error);
    return error ? file_path == std::filesystem::u8path(path_) : equivalent;
  }
  std::string line_directive(unsigned position) const {
    const auto line =
        std::count(source_.begin(), source_.begin() + std::min<std::size_t>(position, source_.size()), '\n') + 1;
    std::string file;
    for (char c : path_) {
      if (c == '\\' || c == '"')
        file += '\\';
      file += c;
    }
    return "\n#line " + std::to_string(line) + " \"" + file + "\"\n";
  }
  std::set<std::string> expanded_identifiers(const std::vector<Token> &action_tokens) const;
  std::string cleanup(std::size_t first, std::size_t last, unsigned position) const;
  void index_control_flow(CXCursor cursor, std::vector<unsigned> scopes = {},
                          unsigned limit = std::numeric_limits<unsigned>::max());
  void visit(CXCursor cursor, CXCursorKind parent = CXCursor_UnexposedDecl);
  void require_keyword(unsigned position, std::string_view keyword) const {
    if (!is_keyword_at(source_, position, keyword))
      fail("control transfer from a macro cannot be transformed (byte " + std::to_string(position) + ")");
  }

  CXTranslationUnit unit_;
  std::string path_;
  std::string source_;
  std::vector<Edit> edits_;
  std::map<unsigned, CXCursor> macros_;
  std::map<std::string, std::vector<CXCursor>> macro_definitions_;
  std::set<unsigned> seen_;
  std::vector<Scope> scopes_;
  std::vector<std::pair<bool, std::size_t>> controls_; // true for loops, false for switches
  bool function_void_ = false;
  std::string function_prelude_;
  std::string return_type_;
  std::map<std::string, std::pair<unsigned, std::vector<unsigned>>> labels_;
  std::map<unsigned, unsigned> defer_scopes_;
  std::vector<Declaration> declarations_;
  std::vector<Directive> directives_;
};

std::set<std::string> Transformer::expanded_identifiers(const std::vector<Token> &action_tokens) const {
  std::set<std::string> found, visited;
  std::vector<std::string> pending;
  for (const auto &token : action_tokens)
    if (token.kind == CXToken_Identifier && found.insert(token.text).second)
      pending.push_back(token.text);
  const std::set<std::string> forbidden = {"return", "goto", "break", "continue", "defer"};
  while (!pending.empty()) {
    auto name = pending.back();
    pending.pop_back();
    if (name == "__COUNTER__")
      fail("__COUNTER__ in deferred cleanup cannot be duplicated safely");
    if (!visited.insert(name).second)
      continue;
    auto definitions = macro_definitions_.find(name);
    if (definitions == macro_definitions_.end())
      continue;
    for (auto definition : definitions->second) {
      auto body = tokens(unit_, clang_getCursorExtent(definition));
      if (body.empty())
        continue;
      std::size_t first = 1;
      std::set<std::string> parameters;
      if (body.size() > 1 && body[1].text == "(" && body[0].finish == body[1].start) {
        first = 2;
        while (first < body.size() && body[first].text != ")") {
          if (body[first].kind == CXToken_Identifier)
            parameters.insert(body[first].text);
          ++first;
        }
        ++first;
      }
      for (; first < body.size(); ++first) {
        const auto &token = body[first];
        if (token.text == "##")
          fail("cleanup macro '" + name + "' uses token pasting");
        if (forbidden.count(token.text))
          fail("cleanup macro '" + name + "' contains control transfer");
        if (token.kind == CXToken_Identifier && !parameters.count(token.text) && found.insert(token.text).second)
          pending.push_back(token.text);
      }
    }
  }
  return found;
}

std::string Transformer::cleanup(std::size_t first, std::size_t last, unsigned position) const {
  std::string result;
  std::vector<unsigned> current;
  for (const auto &scope : scopes_)
    current.push_back(scope.start);
  for (std::size_t index = last; index > first; --index) {
    const auto &scope = scopes_[index - 1];
    const std::vector<unsigned> owner(current.begin(), current.begin() + static_cast<std::ptrdiff_t>(index));
    for (auto it = scope.actions.rbegin(); it != scope.actions.rend(); ++it) {
      const auto &action = *it;
      if (action.start >= position)
        continue;
      for (const auto &directive : directives_)
        if (action.start < directive.position && directive.position < position &&
            ((directive.kind != "define" && directive.kind != "undef") || action.identifiers.count(directive.name)))
          fail("preprocessor state affecting cleanup may change at byte " + std::to_string(directive.position));
      for (const auto &declaration : declarations_)
        if (action.identifiers.count(declaration.name) && action.start < declaration.position &&
            declaration.position < position && position < declaration.limit && prefix(declaration.scopes, owner) &&
            prefix(current, declaration.scopes))
          fail("cleanup identifier '" + declaration.name + "' may be shadowed at exit byte " +
               std::to_string(position));
      result += "if (" + action.flag + ") { " + line_directive(action.action_start) + action.text + "\n; } " +
                line_directive(position);
    }
  }
  return result;
}

void Transformer::index_control_flow(CXCursor cursor, std::vector<unsigned> scopes, unsigned limit) {
  const auto kind = clang_getCursorKind(cursor);
  const auto start = begin(cursor);
  if (kind == CXCursor_DoStmt && macros_.count(start)) {
    if (!scopes.empty())
      defer_scopes_[start] = scopes.back();
    return;
  }
  if (kind == CXCursor_CompoundStmt)
    scopes.push_back(start);
  if (kind == CXCursor_ForStmt)
    limit = std::min(limit, end(cursor));
  if (kind == CXCursor_LabelStmt)
    labels_[spelling(cursor)] = {start, scopes};
  if (!scopes.empty() && (kind == CXCursor_VarDecl || kind == CXCursor_TypedefDecl ||
                          kind == CXCursor_EnumConstantDecl || kind == CXCursor_StructDecl ||
                          kind == CXCursor_UnionDecl || kind == CXCursor_EnumDecl || kind == CXCursor_FunctionDecl))
    declarations_.push_back({spelling(cursor), start, scopes, limit});
  for (auto child : children(cursor))
    index_control_flow(child, scopes, limit);
}

void Transformer::visit(CXCursor cursor, CXCursorKind parent) {
  const auto kind = clang_getCursorKind(cursor);
  const auto start = begin(cursor), finish = end(cursor);
  if (kind == CXCursor_FunctionDecl && clang_isCursorDefinition(cursor) && in_file(cursor)) {
    function_void_ = clang_getCursorResultType(cursor).kind == CXType_Void;
    function_prelude_.clear();
    return_type_.clear();
    if (!function_void_) {
      const auto function_type = "__ascii_defer_function_" + std::to_string(start);
      function_prelude_ = "typedef __typeof__(&" + spelling(cursor) + ") " + function_type + ";";
      std::string args;
      const int count = clang_Cursor_getNumArguments(cursor);
      for (int i = 0; i < count; ++i) {
        auto parameter = clang_Cursor_getArgument(cursor, static_cast<unsigned>(i));
        const auto name = spelling(parameter);
        if (name.empty())
          fail("unnamed parameter in defer function at byte " + std::to_string(start));
        auto type = clang_getCanonicalType(clang_getCursorType(parameter));
        while (type.kind == CXType_Pointer || type.kind == CXType_ConstantArray ||
               type.kind == CXType_IncompleteArray) {
          type = clang_getCanonicalType(type.kind == CXType_Pointer ? clang_getPointeeType(type)
                                                                    : clang_getArrayElementType(type));
        }
        if (type.kind == CXType_VariableArray || type.kind == CXType_DependentSizedArray)
          fail("variably modified parameter in defer function at byte " + std::to_string(start));
        auto alias = "__ascii_defer_argument_" + std::to_string(start) + "_" + std::to_string(i);
        function_prelude_ += " typedef __typeof__(" + name + ") " + alias + ";";
        if (i)
          args += ", ";
        args += "(" + alias + "){0}";
      }
      return_type_ = "__typeof__(((" + function_type + ")0)(" + args + "))";
    }
    for (auto child : children(cursor))
      visit(child, kind);
    return;
  }
  if (scopes_.empty() && kind != CXCursor_CompoundStmt)
    return;
  auto macro = macros_.find(start);
  if (macro != macros_.end() && kind == CXCursor_DoStmt) {
    if (parent != CXCursor_CompoundStmt)
      fail("defer must be a direct statement in a block");
    const auto macro_end = end(macro->second);
    auto parts = tokens(unit_, clang_getCursorExtent(macro->second));
    auto opening = std::find_if(parts.begin() + std::min<std::size_t>(1, parts.size()), parts.end(),
                                [](const Token &token) { return token.text == "("; });
    if (opening == parts.end() || macro_end <= opening->start || source_[macro_end - 1] != ')')
      fail("cannot parse defer at byte " + std::to_string(start));
    auto raw_action = source_.substr(opening->start + 1, macro_end - opening->start - 2);
    auto action = trim(raw_action);
    if (action.empty())
      fail("empty defer at byte " + std::to_string(start));
    const auto action_start = opening->start + 1 + static_cast<unsigned>(raw_action.find_first_not_of(" \t\r\n\f\v"));
    const std::set<std::string> forbidden = {"return", "goto", "break", "continue", "defer"};
    for (std::size_t i = 1; i < parts.size(); ++i)
      if (forbidden.count(parts[i].text))
        fail("control transfer or nested defer inside cleanup");
    std::vector<Token> action_tokens(parts.begin() + std::min<std::size_t>(1, parts.size()), parts.end());
    auto flag = "__ascii_defer_active_" + std::to_string(start);
    scopes_.back().actions.push_back({start, action_start, flag, action, expanded_identifiers(action_tokens)});
    edits_.push_back({start, static_cast<unsigned>(after_semicolon(source_, macro_end)), flag + " = 1;"});
    seen_.insert(start);
    return;
  }
  if (kind == CXCursor_CompoundStmt) {
    if (parent == CXCursor_FunctionDecl && !function_prelude_.empty())
      edits_.push_back({start + 1, start + 1, function_prelude_ + " "});
    scopes_.push_back({start, {}});
    for (auto child : children(cursor))
      visit(child, kind);
    const auto &actions = scopes_.back().actions;
    if (!actions.empty() && parent == CXCursor_SwitchStmt)
      fail("defer directly in switch body is unsupported; use a case block");
    if (!actions.empty()) {
      std::string declarations;
      for (const auto &action : actions)
        declarations += "int " + action.flag + " = 0; ";
      edits_.push_back({start + 1, start + 1, declarations});
      edits_.push_back({finish - 1, finish - 1, cleanup(scopes_.size() - 1, scopes_.size(), finish)});
    }
    scopes_.pop_back();
    return;
  }
  if (kind == CXCursor_IndirectGotoStmt)
    fail("computed goto cannot be transformed");
  if (kind == CXCursor_GotoStmt) {
    std::vector<std::string> refs;
    for (auto child : children(cursor))
      if (clang_getCursorKind(child) == CXCursor_LabelRef)
        refs.push_back(spelling(child));
    if (refs.size() != 1 || !labels_.count(refs[0]))
      fail("cannot determine goto target");
    const auto &target = labels_.at(refs[0]);
    std::vector<unsigned> current;
    for (const auto &scope : scopes_)
      current.push_back(scope.start);
    if (!prefix(current, target.second))
      fail("goto enters a block with defer analysis");
    if (target.first < start)
      for (const auto &[position, owner] : defer_scopes_)
        if (target.first <= position && position < start &&
            std::find(target.second.begin(), target.second.end(), owner) != target.second.end())
          fail("backward goto can register defer repeatedly");
    auto actions = cleanup(target.second.size(), scopes_.size(), start);
    if (!actions.empty()) {
      require_keyword(start, "goto");
      const auto stop = static_cast<unsigned>(after_semicolon(source_, finish));
      edits_.push_back({start, stop, "{ " + actions + source_.substr(start, stop - start) + " }"});
    }
    return;
  }
  if (kind == CXCursor_ReturnStmt) {
    auto actions = cleanup(0, scopes_.size(), start);
    if (!actions.empty()) {
      require_keyword(start, "return");
      const auto stop = static_cast<unsigned>(after_semicolon(source_, finish));
      auto raw = source_.substr(start + 6, finish - start - 6);
      auto expr = trim(raw);
      if (!expr.empty())
        expr = line_directive(start + 6 + static_cast<unsigned>(raw.find_first_not_of(" \t\r\n\f\v"))) + expr;
      std::string replacement;
      if (!expr.empty() && function_void_)
        replacement = "{ " + expr + "; " + actions + "return; }";
      else if (!expr.empty()) {
        auto temp = "__ascii_defer_return_" + std::to_string(start);
        replacement = "{ " + return_type_ + " " + temp + " = (" + expr + "); " + actions + "return " + temp + "; }";
      } else
        replacement = "{ " + actions + "return; }";
      edits_.push_back({start, stop, replacement});
    }
    return;
  }
  if (kind == CXCursor_BreakStmt || kind == CXCursor_ContinueStmt) {
    auto target = controls_.rbegin();
    if (kind == CXCursor_ContinueStmt)
      target = std::find_if(target, controls_.rend(), [](const auto &control) { return control.first; });
    if (target == controls_.rend())
      fail("cannot determine control-flow target");
    auto actions = cleanup(target->second, scopes_.size(), start);
    if (!actions.empty()) {
      require_keyword(start, kind == CXCursor_BreakStmt ? "break" : "continue");
      const auto stop = static_cast<unsigned>(after_semicolon(source_, finish));
      edits_.push_back({start, stop, "{ " + actions + source_.substr(start, stop - start) + " }"});
    }
    return;
  }
  const bool loop = kind == CXCursor_ForStmt || kind == CXCursor_WhileStmt || kind == CXCursor_DoStmt;
  const bool control = loop || kind == CXCursor_SwitchStmt;
  if (control)
    controls_.push_back({loop, scopes_.size()});
  for (auto child : children(cursor))
    visit(child, kind);
  if (control)
    controls_.pop_back();
}

std::string Transformer::run() {
  for (unsigned i = 0; i < clang_getNumDiagnostics(unit_); ++i) {
    auto diagnostic = clang_getDiagnostic(unit_, i);
    const auto severity = clang_getDiagnosticSeverity(diagnostic);
    auto message = consume_string(clang_formatDiagnostic(diagnostic, clang_defaultDiagnosticDisplayOptions()));
    clang_disposeDiagnostic(diagnostic);
    if (severity >= CXDiagnostic_Error)
      fail("Clang could not parse source: " + message);
  }
  for (auto cursor : children(clang_getTranslationUnitCursor(unit_))) {
    if (clang_getCursorKind(cursor) != CXCursor_FunctionDecl || !clang_isCursorDefinition(cursor) || !in_file(cursor))
      continue;
    bool has_defer = false;
    for (const auto &[position, ignored] : macros_)
      if (begin(cursor) <= position && position < end(cursor)) {
        has_defer = true;
        break;
      }
    if (!has_defer)
      continue;
    labels_.clear();
    defer_scopes_.clear();
    declarations_.clear();
    index_control_flow(cursor);
    visit(cursor);
  }
  if (seen_.size() != macros_.size())
    fail("unhandled defer macro expansion");
  std::stable_sort(edits_.begin(), edits_.end(), [](const Edit &a, const Edit &b) {
    return std::tie(a.start, a.finish) > std::tie(b.start, b.finish);
  });
  auto output = source_;
  for (const auto &edit : edits_)
    output.replace(edit.start, edit.finish - edit.start, edit.replacement + line_directive(edit.finish));
  if (output.compare(0, 3, "\xef\xbb\xbf") == 0)
    return output.substr(0, 3) + line_directive(0) + output.substr(3);
  return line_directive(0) + output;
}

} // namespace

std::string transform(CXTranslationUnit unit, const std::string &path, const std::string &source) {
  return Transformer(unit, path, source).run();
}

} // namespace defer_tool
