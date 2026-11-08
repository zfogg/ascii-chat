#include <clang-c/Index.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/Support/CommandLine.h>
#include <charconv>
#include <string>

static_assert(LLVM_VERSION_MAJOR >= 19, "LLVM 19 or newer is required");

static llvm::cl::opt<bool> check("check", llvm::cl::desc("Verify libclang can create an index"));

int main(int argc, char **argv) {
  llvm::cl::ParseCommandLineOptions(argc, argv, "LLVM development library check\n");
  CXString clang_version = clang_getClangVersion();
  const char *raw_version = clang_getCString(clang_version);
  std::string version = raw_version ? raw_version : "";
  clang_disposeString(clang_version);
  const auto start = version.find("clang version ");
  int clang_major = 0;
  if (start == std::string::npos)
    return 1;
  const auto number = version.data() + start + sizeof("clang version ") - 1;
  if (std::from_chars(number, version.data() + version.size(), clang_major).ec != std::errc{} ||
      clang_major != LLVM_VERSION_MAJOR)
    return 1;
  CXIndex index = clang_createIndex(0, 0);
  if (!index)
    return 1;
  clang_disposeIndex(index);
  return 0;
}
