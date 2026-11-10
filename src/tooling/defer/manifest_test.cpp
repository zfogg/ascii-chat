#include "manifest.h"
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#define CHECK(condition)                                                                                               \
  do {                                                                                                                 \
    if (!(condition)) {                                                                                                \
      std::fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #condition);                               \
      return 1;                                                                                                        \
    }                                                                                                                  \
  } while (0)

int main() {
  auto manifest = defer_tool::Manifest::parse("a.c\tout/a.c\r\nb.c\tout/b.c\t[\"-I\",\"dir with spaces\"]\n", false);
  CHECK(manifest.records().size() == 2);
  CHECK(manifest.records()[0].source.generic_u8string() == "a.c");
  CHECK(manifest.records()[0].arguments.empty());
  CHECK(manifest.records()[1].output.generic_u8string() == "out/b.c");
  CHECK(manifest.records()[1].arguments.size() == 2);
  CHECK(manifest.records()[1].arguments[1] == "dir with spaces");
  CHECK(defer_tool::Manifest::parse("a.c\tout.c\t[\"x\\u2028y\"]\n", false).records()[0].arguments[0] ==
        "x\xe2\x80\xa8y");
  CHECK(defer_tool::Manifest::parse("", true).records().empty());
  for (const auto &text : std::vector<std::string>{"missing-tab", "\tout.c", "a.c\t", "a.c\tout\t{}", "a.c\tout\t[1]",
                                                   "a.c\tout\t[\"bad\\u0000value\"]", "a.c\tout\t[]\textra"}) {
    bool rejected = false;
    try {
      (void)defer_tool::Manifest::parse(text, false);
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    CHECK(rejected);
  }
  bool rejected = false;
  try {
    (void)defer_tool::Manifest::parse("a.c\tout\t[]", true);
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  CHECK(rejected);
  return 0;
}
