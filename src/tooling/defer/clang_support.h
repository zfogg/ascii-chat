#pragma once

#include <clang-c/Index.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace defer_tool {

inline std::string consume_string(CXString value) {
  const char *text = clang_getCString(value);
  std::string result = text ? text : "";
  clang_disposeString(value);
  return result;
}

class ClangIndex {
public:
  ClangIndex() : value_(clang_createIndex(0, 0)) {
    if (!value_)
      throw std::runtime_error("cannot create libclang index");
  }
  ~ClangIndex() {
    clang_disposeIndex(value_);
  }
  ClangIndex(const ClangIndex &) = delete;
  ClangIndex &operator=(const ClangIndex &) = delete;
  CXIndex get() const {
    return value_;
  }

private:
  CXIndex value_;
};

class TranslationUnit {
public:
  TranslationUnit(CXIndex index, const std::string &file, const std::vector<const char *> &arguments,
                  CXUnsavedFile *unsaved = nullptr, unsigned unsaved_count = 0, bool detailed = true) {
    const auto error = clang_parseTranslationUnit2(
        index, file.c_str(), arguments.data(), static_cast<int>(arguments.size()), unsaved, unsaved_count,
        detailed ? CXTranslationUnit_DetailedPreprocessingRecord : 0, &value_);
    if (error != CXError_Success || !value_)
      throw std::runtime_error("libclang could not parse " + file);
  }
  ~TranslationUnit() {
    clang_disposeTranslationUnit(value_);
  }
  TranslationUnit(const TranslationUnit &) = delete;
  TranslationUnit &operator=(const TranslationUnit &) = delete;
  CXTranslationUnit get() const {
    return value_;
  }
  std::vector<std::string> errors() const {
    std::vector<std::string> result;
    for (unsigned i = 0; i < clang_getNumDiagnostics(value_); ++i) {
      CXDiagnostic diagnostic = clang_getDiagnostic(value_, i);
      if (clang_getDiagnosticSeverity(diagnostic) >= CXDiagnostic_Error) {
        CXString filename;
        unsigned line = 0, column = 0;
        clang_getPresumedLocation(clang_getDiagnosticLocation(diagnostic), &filename, &line, &column);
        const auto name = consume_string(filename);
        result.push_back(name + ":" + std::to_string(line) + ":" + std::to_string(column) + ": " +
                         consume_string(clang_getDiagnosticSpelling(diagnostic)));
      }
      clang_disposeDiagnostic(diagnostic);
    }
    return result;
  }

private:
  CXTranslationUnit value_ = nullptr;
};

inline void validate_generated(CXIndex index, const std::string &file, const std::vector<const char *> &arguments,
                               const std::string &content) {
  CXUnsavedFile generated{file.c_str(), content.data(), static_cast<unsigned long>(content.size())};
  TranslationUnit validation(index, file, arguments, &generated, 1, false);
  const auto diagnostics = validation.errors();
  if (!diagnostics.empty())
    throw std::runtime_error("generated C is invalid: " + diagnostics.front());
}

inline std::vector<CXCursor> children(CXCursor parent) {
  std::vector<CXCursor> result;
  clang_visitChildren(
      parent,
      [](CXCursor child, CXCursor, CXClientData data) {
        static_cast<std::vector<CXCursor> *>(data)->push_back(child);
        return CXChildVisit_Continue;
      },
      &result);
  return result;
}

} // namespace defer_tool
