#include "clang_support.h"
#include "transform.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <llvm/Support/Process.h>
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

static std::string transform_source(std::string source) {
  defer_tool::ClangIndex index;
  const std::vector<const char *> args = {"-std=c17"};
  for (unsigned pass = 0; pass < 64; ++pass) {
    CXUnsavedFile file = {"memory.c", source.c_str(), static_cast<unsigned long>(source.size())};
    defer_tool::TranslationUnit unit(index.get(), "memory.c", args, &file, 1);
    auto expanded = defer_tool::expand_defer_wrappers(unit.get(), "memory.c", source);
    if (expanded == source)
      return defer_tool::transform(unit.get(), "memory.c", source);
    source = std::move(expanded);
  }
  throw std::runtime_error("wrapper expansion limit");
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
  output = transform_source(prefix + "int f(int) { defer((void)0); return 7; }\n");
  CHECK(output.find("__ascii_defer_argument_") != std::string::npos);
  CHECK(output.find("((int){0})") != std::string::npos);
  output = transform_source(
      prefix + "#define INNER(x) defer(x++)\n#define OUTER(x) INNER(x)\nvoid f(void) { int n=0; OUTER(n); }\n");
  CHECK(output.find("__ascii_defer_active_") != std::string::npos);
  CHECK(output.find("n ++") != std::string::npos);
  output = transform_source(prefix + "int f(int (*)(int)) { defer((void)0); return 7; }\n");
  CHECK(output.find("__ascii_defer_argument_") != std::string::npos);
  for (const auto &source : std::vector<std::string>{
           "void f(int x) { again: defer(x++); if (x) goto again; }\n", "void f(int x) { if (x) defer(x++); }\n",
           "void f(int x) { defer({ if (x) return; }); }\n", "void f(void) { defer_cancel(missing); }\n",
           "struct S { int n; }; int f(struct S) { defer((void)0); return 7; }\n",
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
  {
    namespace fs = std::filesystem;
    const auto dir =
        fs::temp_directory_path() / ("ascii-defer-header-test-" + std::to_string(llvm::sys::Process::getProcessId()));
    fs::create_directories(dir);
    const auto header = dir / "local.h";
    const auto main_file = dir / "main.c";
    const auto marker = dir / "marker.h";
    const std::string header_text = "#if ENABLE_CLEANUP\nstatic inline void clean(int *x) { defer((*x)++); }\n#endif\n";
    const std::string main_text =
        "#include \"marker.h\"\n#define ENABLE_CLEANUP 1\n#include \"local.h\"\nvoid f(int *x) { defer((*x)++); }\n";
    std::ofstream(marker) << "void __ascii_defer_marker(void);\n"
                             "#define defer(...) do { __ascii_defer_marker(); } while(0)\n";
    std::ofstream(header, std::ios::binary) << header_text;
    std::ofstream(main_file, std::ios::binary) << main_text;
    defer_tool::ClangIndex index;
    const auto include = dir.generic_u8string();
    const std::vector<const char *> args = {"-std=c17", "-I", include.c_str()};
    defer_tool::TranslationUnit unit(index.get(), main_file.generic_u8string(), args);
    std::string transformed_header, transformed_main;
    try {
      transformed_header =
          defer_tool::transform_translation_unit_file(unit.get(), header.generic_u8string(), header_text);
      transformed_main =
          defer_tool::transform_translation_unit_file(unit.get(), main_file.generic_u8string(), main_text);
    } catch (const std::exception &error) {
      std::fprintf(stderr, "header test error: %s\n", error.what());
      return 1;
    }
    CHECK(transformed_header.find("__ascii_defer_active_") != std::string::npos);
    CHECK(transformed_main.find("__ascii_defer_active_") != std::string::npos);
    const auto header_name = header.generic_u8string();
    const auto main_name = main_file.generic_u8string();
    CXUnsavedFile rewritten[] = {
        {header_name.c_str(), transformed_header.data(), static_cast<unsigned long>(transformed_header.size())},
        {main_name.c_str(), transformed_main.data(), static_cast<unsigned long>(transformed_main.size())},
    };
    defer_tool::TranslationUnit validation(index.get(), main_name, args, rewritten, 2);
    CHECK(validation.errors().empty());
    const std::string disabled_main = "#include \"marker.h\"\n#define ENABLE_CLEANUP 0\n#include \"local.h\"\n";
    CXUnsavedFile disabled{main_name.c_str(), disabled_main.data(), static_cast<unsigned long>(disabled_main.size())};
    defer_tool::TranslationUnit disabled_unit(index.get(), main_name, args, &disabled, 1);
    const auto disabled_header =
        defer_tool::transform_translation_unit_file(disabled_unit.get(), header_name, header_text);
    CHECK(disabled_header.find("__ascii_defer_active_") == std::string::npos);
    fs::remove(header);
    fs::remove(main_file);
    fs::remove(marker);
    fs::remove(dir);
  }
  return 0;
}
