#include "clang_support.h"
#include "transform.h"

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

static std::string transform_source(const std::string &source) {
  defer_tool::ClangIndex index;
  CXUnsavedFile file = {"memory.c", source.c_str(), static_cast<unsigned long>(source.size())};
  const std::vector<const char *> args = {"-std=c17"};
  defer_tool::TranslationUnit unit(index.get(), "memory.c", args, &file, 1);
  return defer_tool::transform(unit.get(), "memory.c", source);
}

int main() {
  const std::string prefix = "#define defer(...) do {} while(0)\n"
                             "#define defer_named(name, ...) do {} while(0)\n"
                             "#define defer_cancel(name) do {} while(0)\n";
  auto output = transform_source(prefix + "void f(void) { int x = 1; defer(x++); return; }\n");
  CHECK(output.find("__ascii_defer_active_") != std::string::npos);
  CHECK(output.find("if (__ascii_defer_active_") != std::string::npos);
  CHECK(output.find("x++") != std::string::npos);
  output = transform_source(prefix + "void f(void) { int x = 1; defer_named(h, x++); defer_cancel(h); }\n");
  CHECK(output.find(" = 0;") != std::string::npos);
  CHECK(output.find("x++") != std::string::npos);
  for (const auto &source : std::vector<std::string>{
           "void f(int x) { again: defer(x++); if (x) goto again; }\n", "void f(int x) { if (x) defer(x++); }\n",
           "void f(int x) { defer({ if (x) return; }); }\n", "void f(void) { defer_cancel(missing); }\n",
           "void f(int x) { defer_named(h, x++); defer_named(h, x++); }\n",
           "void f(int x) { switch(x) { case 0: ; defer(x++); break; case 1: break; } }\n",
           "#define break_if(x) if (x) break\nvoid f(int x) { while (x) { defer(x++); break_if(x); } }\n"}) {
    bool rejected = false;
    try {
      (void)transform_source(prefix + source);
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    CHECK(rejected);
  }
  return 0;
}
