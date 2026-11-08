#include "text.h"
#include <cstdio>
#include <stdexcept>
#include <string_view>

#define CHECK(condition)                                                                                               \
  do {                                                                                                                 \
    if (!(condition)) {                                                                                                \
      std::fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #condition);                               \
      return 1;                                                                                                        \
    }                                                                                                                  \
  } while (0)

int main() {
  using namespace defer_tool;
  CHECK(may_contain_defer("defer /* comment (x) */ (cleanup());"));
  CHECK(may_contain_defer("defer // comment\n(cleanup());"));
  CHECK(may_contain_defer("defer \\\n(cleanup());"));
  CHECK(!may_contain_defer("not_defer(x); deferred(x);"));
  CHECK(!may_contain_defer("defer /* unterminated"));
  CHECK(is_keyword_at("break;", 0, "break"));
  CHECK(!is_keyword_at("break_if(x)", 0, "break"));
  CHECK(!is_keyword_at("return2;", 0, "return"));
  CHECK(is_keyword_at(" return;", 1, "return"));
  CHECK(after_semicolon(" \t/* ; */ // comment\n ;next", 0) == 23);
  CHECK(escape_depfile("C:\\test dir\\a#$:b") == "C\\:/test\\ dir/a\\#$$\\:b");
  bool rejected = false;
  try {
    (void)after_semicolon(" /* unterminated", 0);
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  CHECK(rejected);
  return 0;
}