#include "clang_support.h"
#include "transform.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, std::size_t size) {
  if (size > 65536)
    return 0;
  const std::string path = "defer-fuzz-input.c";
  const std::string source =
      "#define defer(...) do {} while (0)\n" + std::string(reinterpret_cast<const char *>(data), size);
  const std::vector<const char *> arguments = {"-std=c17", "-DASCIICHAT_DEFER_PARSING=1"};
  try {
    defer_tool::ClangIndex index;
    CXUnsavedFile input{path.c_str(), source.data(), static_cast<unsigned long>(source.size())};
    defer_tool::TranslationUnit original(index.get(), path, arguments, &input, 1);
    const auto output = defer_tool::transform(original.get(), path, source);
    defer_tool::validate_generated(index.get(), path, arguments, output);
  } catch (const std::exception &) {
    // Invalid input and unsupported control flow must fail closed.
  }
  return 0;
}
