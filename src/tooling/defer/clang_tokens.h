#pragma once

#include "clang_support.h"
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace defer_tool::detail {
inline unsigned offset(CXSourceLocation location) {
  unsigned result = 0;
  clang_getExpansionLocation(location, nullptr, nullptr, nullptr, &result);
  return result;
}
inline unsigned begin(CXCursor cursor) {
  return offset(clang_getRangeStart(clang_getCursorExtent(cursor)));
}
inline unsigned end(CXCursor cursor) {
  return offset(clang_getRangeEnd(clang_getCursorExtent(cursor)));
}
inline std::string spelling(CXCursor cursor) {
  return consume_string(clang_getCursorSpelling(cursor));
}

struct Token {
  std::string text;
  CXTokenKind kind;
  unsigned start;
  unsigned finish;
};

inline std::vector<Token> tokens(CXTranslationUnit unit, CXSourceRange extent) {
  CXToken *raw = nullptr;
  unsigned count = 0;
  // libclang initializes both output parameters; count is not a local constant.
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

inline std::string trim(std::string value) {
  auto whitespace = [](unsigned char c) { return std::isspace(c) != 0; };
  value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), whitespace));
  value.erase(std::find_if_not(value.rbegin(), value.rend(), whitespace).base(), value.end());
  return value;
}

struct Edit {
  unsigned start;
  unsigned finish;
  std::string replacement;
};
} // namespace defer_tool::detail
