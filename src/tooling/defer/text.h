#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

namespace defer_tool {

// Source offsets are byte offsets, matching libclang's source locations.
inline bool is_identifier_byte(unsigned char value) {
  return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') ||
         value == '_' || value >= 128;
}

inline bool is_keyword_at(std::string_view source, std::size_t offset, std::string_view keyword) {
  if (offset > source.size() || source.substr(offset, keyword.size()) != keyword) {
    return false;
  }
  auto end = offset + keyword.size();
  while (source.substr(end, 2) == "\\\n" || source.substr(end, 3) == "\\\r\n")
    end += source[end + 1] == '\r' ? 3 : 2;
  return end == source.size() || !is_identifier_byte(static_cast<unsigned char>(source[end]));
}

inline std::size_t after_semicolon(std::string_view source, std::size_t offset) {
  while (offset < source.size()) {
    const char c = source[offset];
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v') {
      ++offset;
    } else if (source.substr(offset, 2) == "/*") {
      const auto end = source.find("*/", offset + 2);
      if (end == source.npos)
        throw std::runtime_error("unterminated comment");
      offset = end + 2;
    } else if (source.substr(offset, 2) == "//") {
      const auto end = source.find('\n', offset + 2);
      offset = end == source.npos ? source.size() : end + 1;
    } else {
      break;
    }
  }
  if (offset == source.size() || source[offset] != ';')
    throw std::runtime_error("expected semicolon");
  return offset + 1;
}

// Detect comment-separated invocations without interpreting C string literals.
// False positives are safe: the libclang pass decides whether an expansion exists.
inline bool may_contain_defer(std::string_view source) {
  std::string unspliced;
  unspliced.reserve(source.size());
  for (std::size_t i = 0; i < source.size(); ++i) {
    if (source.substr(i, 2) == "\\\n") { ++i; continue; }
    if (source.substr(i, 3) == "\\\r\n") { i += 2; continue; }
    unspliced += source[i];
  }
  source = unspliced;
  std::size_t search = 0;
  while ((search = source.find("defer", search)) != source.npos) {
    const auto start = search++;
    if ((start && is_identifier_byte(static_cast<unsigned char>(source[start - 1]))) ||
        !is_keyword_at(source, start, "defer"))
      continue;
    std::size_t pos = start + 5;
    while (pos < source.size()) {
      const auto c = source[pos];
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v') {
        ++pos;
      } else if (source.substr(pos, 2) == "/*") {
        const auto end = source.find("*/", pos + 2);
        if (end == source.npos)
          break;
        pos = end + 2;
      } else if (source.substr(pos, 2) == "//") {
        const auto end = source.find('\n', pos + 2);
        pos = end == source.npos ? source.size() : end + 1;
      } else if (source.substr(pos, 2) == "\\\n") {
        pos += 2;
      } else if (source.substr(pos, 3) == "\\\r\n") {
        pos += 3;
      } else {
        break;
      }
    }
    if (pos < source.size() && source[pos] == '(')
      return true;
  }
  return false;
}

inline std::string escape_depfile(std::string_view value) {
  std::string escaped;
  for (char c : value) {
    if (c == '\\')
      escaped += '/';
    else if (c == '$')
      escaped += "$$";
    else if (c == '#' || c == ' ' || c == ':') {
      escaped += '\\';
      escaped += c;
    } else
      escaped += c;
  }
  return escaped;
}

} // namespace defer_tool
