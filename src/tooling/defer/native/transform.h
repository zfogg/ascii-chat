#pragma once

#include <clang-c/Index.h>
#include <string>

namespace defer_tool {

// Returns transformed UTF-8 bytes or throws if cleanup semantics cannot be preserved.
std::string transform(CXTranslationUnit unit, const std::string &path, const std::string &source);

} // namespace defer_tool
