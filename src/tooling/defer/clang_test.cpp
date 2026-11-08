#include "clang_support.h"
#include <cstdio>
#include <cstring>
#define CHECK(condition)                                                                                               \
  do {                                                                                                                 \
    if (!(condition)) {                                                                                                \
      std::fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #condition);                               \
      return 1;                                                                                                        \
    }                                                                                                                  \
  } while (0)

int main() {
  defer_tool::ClangIndex index;
  const char code[] = "int answer(void) { return 42; }";
  CXUnsavedFile input = {"memory.c", code, sizeof(code) - 1};
  const std::vector<const char *> args = {"-std=c17"};
  defer_tool::TranslationUnit unit(index.get(), "memory.c", args, &input, 1);
  CHECK(unit.errors().empty());
  bool found = false;
  for (const auto cursor : defer_tool::children(clang_getTranslationUnitCursor(unit.get())))
    if (clang_getCursorKind(cursor) == CXCursor_FunctionDecl &&
        defer_tool::consume_string(clang_getCursorSpelling(cursor)) == "answer")
      found = true;
  CHECK(found);
  const char bad_code[] = "int broken( {";
  CXUnsavedFile invalid = {"invalid.c", bad_code, sizeof(bad_code) - 1};
  defer_tool::TranslationUnit bad(index.get(), "invalid.c", args, &invalid, 1);
  CHECK(!bad.errors().empty());
  return 0;
}