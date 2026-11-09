#pragma once

#include <clang-c/Index.h>
#include <string>

namespace defer_tool {

// Returns transformed UTF-8 bytes or throws if cleanup semantics cannot be preserved.
std::string transform(CXTranslationUnit unit, const std::string &path, const std::string &source, bool objc = false);
// Analyze one file in a larger translation unit without modifying other files.
std::string transform_translation_unit_file(CXTranslationUnit unit, const std::string &path, const std::string &source,
                                            const std::string &display_path = {}, bool objc = false);
std::string expand_defer_wrappers(CXTranslationUnit unit, const std::string &path, const std::string &source,
                                  bool allow_other_files = false);
std::string explain(CXTranslationUnit unit, const std::string &path, const std::string &source, unsigned line,
                    bool objc = false);
std::string show_transformed(CXTranslationUnit unit, const std::string &path, const std::string &source, unsigned line,
                             bool objc = false);

} // namespace defer_tool
