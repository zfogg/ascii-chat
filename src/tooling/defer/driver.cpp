#include "driver.h"
#include "text.h"
#include "transform.h"

#include <clang-c/CXCompilationDatabase.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Process.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/SHA256.h>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace defer_tool {
namespace fs = std::filesystem;
namespace {

struct CompilationDatabase {
  CXCompilationDatabase value = nullptr;
  explicit CompilationDatabase(const fs::path &directory) {
    CXCompilationDatabase_Error error;
    value = clang_CompilationDatabase_fromDirectory(directory.u8string().c_str(), &error);
    if (!value || error != CXCompilationDatabase_NoError)
      throw std::runtime_error("cannot load compilation database: " + directory.u8string());
  }
  ~CompilationDatabase() {
    clang_CompilationDatabase_dispose(value);
  }
};

std::string compiler_resource_dir(const std::string &compiler) {
  static std::map<std::string, std::string> cached;
  static std::mutex cache_mutex;
  std::lock_guard<std::mutex> lock(cache_mutex);
  const auto key = fs::absolute(fs::u8path(compiler)).u8string();
  if (cached.count(key))
    return cached.at(key);
  auto program = llvm::sys::findProgramByName(compiler);
  if (!program)
    throw std::runtime_error("cannot find compiler: " + compiler);
  llvm::SmallString<128> temporary;
  int fd = -1;
  if (auto error = llvm::sys::fs::createTemporaryFile("ascii-defer-resource", "txt", fd, temporary))
    throw std::runtime_error("cannot create resource directory output: " + error.message());
  if (auto error = llvm::sys::Process::SafelyCloseFileDescriptor(fd))
    throw std::runtime_error("cannot close resource directory output: " + error.message());
  std::string temp_path(temporary.str());
  const llvm::StringRef arguments[] = {*program, "-print-resource-dir"};
  const std::optional<llvm::StringRef> redirects[] = {std::nullopt, llvm::StringRef(temp_path), std::nullopt};
  const int result = llvm::sys::ExecuteAndWait(*program, arguments, std::nullopt, redirects);
  std::string resource;
  try {
    resource = read_file(fs::u8path(temp_path));
  } catch (...) {
    std::error_code ignored;
    fs::remove(fs::u8path(temp_path), ignored);
    throw;
  }
  std::error_code ignored;
  fs::remove(fs::u8path(temp_path), ignored);
  while (!resource.empty() && (resource.back() == '\n' || resource.back() == '\r' || resource.back() == ' '))
    resource.pop_back();
  if (result != 0 || resource.empty())
    throw std::runtime_error("cannot determine compiler resource directory: " + compiler);
  cached.emplace(key, resource);
  return resource;
}

std::vector<std::string> compile_arguments(const fs::path &database, const fs::path &generated,
                                           const fs::path &source) {
  CompilationDatabase db(database);
  CXCompileCommands commands = clang_CompilationDatabase_getCompileCommands(db.value, generated.u8string().c_str());
  if (!commands || clang_CompileCommands_getSize(commands) != 1) {
    if (commands)
      clang_CompileCommands_dispose(commands);
    throw std::runtime_error("expected one compile command for " + generated.u8string());
  }
  const auto command = clang_CompileCommands_getCommand(commands, 0);
  const auto directory = consume_string(clang_CompileCommand_getDirectory(command));
  std::vector<std::string> args;
  for (unsigned i = 0; i < clang_CompileCommand_getNumArgs(command); ++i)
    args.push_back(consume_string(clang_CompileCommand_getArg(command, i)));
  clang_CompileCommands_dispose(commands);
  if (args.empty())
    throw std::runtime_error("empty compile command for " + generated.u8string());
  const auto compiler = args.front();
  const auto working_directory = fs::u8path(directory);
  std::vector<std::string> result = {"-working-directory", directory, "-iquote", source.parent_path().u8string()};
  for (std::size_t i = 1; i < args.size();) {
    const auto &value = args[i];
    if (value == "-Xclang" && i + 3 < args.size() && args[i + 1] == "-include-pch" && args[i + 2] == "-Xclang") {
      i += 4;
      continue;
    }
    if (value == "-o" || value == "-MF" || value == "-MT" || value == "-MQ") {
      i += 2;
      continue;
    }
    if (value == "--" || value == "-c" || value == "-MD" || value == "-MMD" || value == "-MP" ||
        (!value.empty() && value[0] != '-' &&
         (working_directory / fs::u8path(value)).lexically_normal() == generated)) {
      ++i;
      continue;
    }
    result.push_back(value);
    ++i;
  }
  if (std::none_of(result.begin(), result.end(), [](const auto &arg) { return arg.rfind("-resource-dir", 0) == 0; })) {
    result.push_back("-resource-dir");
    result.push_back(compiler_resource_dir(compiler));
  }
  return result;
}

} // namespace

std::string read_file(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("cannot open " + path.u8string());
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void verify_libclang(const fs::path &requested) {
  if (!fs::is_regular_file(requested))
    throw std::runtime_error("libclang not found: " + requested.u8string());
  fs::path loaded;
#ifdef _WIN32
  HMODULE module = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(&clang_createIndex), &module))
    throw std::runtime_error("cannot determine the loaded libclang module");
  std::wstring buffer(32768, L'\0');
  const auto length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (!length || length == buffer.size())
    throw std::runtime_error("cannot determine the loaded libclang path");
  buffer.resize(length);
  loaded = fs::path(buffer);
#else
  Dl_info info{};
  if (!dladdr(reinterpret_cast<const void *>(&clang_createIndex), &info) || !info.dli_fname)
    throw std::runtime_error("cannot determine the loaded libclang path");
  loaded = fs::u8path(info.dli_fname);
#endif
  std::error_code error;
  if (fs::equivalent(requested, loaded, error) && !error)
    return;
  if (fs::file_size(requested) != fs::file_size(loaded))
    throw std::runtime_error("--libclang differs from the library loaded by the native executable");
  const auto digest = [](const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
      throw std::runtime_error("cannot read libclang: " + path.u8string());
    llvm::SHA256 hash;
    char buffer[65536];
    while (input.read(buffer, sizeof(buffer)) || input.gcount())
      hash.update(
          llvm::ArrayRef<uint8_t>(reinterpret_cast<const uint8_t *>(buffer), static_cast<std::size_t>(input.gcount())));
    if (input.bad())
      throw std::runtime_error("cannot read libclang: " + path.u8string());
    return hash.final();
  };
  if (digest(requested) != digest(loaded))
    throw std::runtime_error("--libclang differs from the library loaded by the native executable");
}

void write_if_changed(const fs::path &path, const std::string &contents) {
  if (!path.parent_path().empty())
    fs::create_directories(path.parent_path());
  if (fs::exists(path) && read_file(path) == contents)
    return;
  const auto temporary =
      fs::u8path(path.u8string() + "." + std::to_string(llvm::sys::Process::getProcessId()) + ".tmp");
  {
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file || !file.write(contents.data(), static_cast<std::streamsize>(contents.size())))
      throw std::runtime_error("cannot write " + temporary.u8string());
    file.close();
    if (!file)
      throw std::runtime_error("cannot close " + temporary.u8string());
  }
  if (auto error = llvm::sys::fs::rename(temporary.u8string(), path.u8string()))
    throw std::runtime_error("cannot replace output: " + error.message());
}

static std::unique_ptr<TranslationUnit> parse_wrapped(CXIndex index, const std::string &source_name,
                                                      const std::vector<const char *> &arguments,
                                                      std::string &content) {
  for (unsigned pass = 0; pass < 64; ++pass) {
    CXUnsavedFile unsaved{source_name.c_str(), content.data(), static_cast<unsigned long>(content.size())};
    auto unit = std::make_unique<TranslationUnit>(index, source_name, arguments, &unsaved, 1);
    auto expanded = expand_defer_wrappers(unit->get(), source_name, content);
    if (expanded == content)
      return unit;
    content = std::move(expanded);
  }
  throw std::runtime_error("defer wrapper expansion exceeded the recursion limit");
}

FileDependencies transform_file(ClangIndex &index, const fs::path &source_path, const fs::path &output_path,
                                const DriverOptions &options) {
  const auto source = fs::weakly_canonical(fs::absolute(source_path));
  const auto output = fs::weakly_canonical(fs::absolute(output_path));
  if (source == output || (fs::exists(source) && fs::exists(output) && fs::equivalent(source, output)))
    throw std::runtime_error("output must not overwrite the input source");
  auto arguments = options.compilation_database.empty()
                       ? options.clang_arguments
                       : compile_arguments(options.compilation_database, output, source);
  arguments.push_back("-DASCIICHAT_DEFER_PARSING=1");
  std::vector<const char *> raw;
  for (const auto &arg : arguments)
    raw.push_back(arg.c_str());
  auto content = read_file(source);
  const auto source_name = source.u8string();
  auto unit = parse_wrapped(index.get(), source_name, raw, content);
  auto transformed = transform(unit->get(), source.u8string(), content);
  validate_generated(index.get(), source_name, raw, transformed);
  FileDependencies dependencies{output, {source.u8string()}};
  clang_getInclusions(
      unit->get(),
      [](CXFile file, CXSourceLocation *, unsigned, CXClientData opaque) {
        auto *files = static_cast<std::set<std::string> *>(opaque);
        files->insert(fs::absolute(fs::u8path(consume_string(clang_getFileName(file)))).lexically_normal().u8string());
      },
      &dependencies.inputs);
  write_if_changed(output, transformed);
  return dependencies;
}

std::string inspect_file(ClangIndex &index, const fs::path &source_path, unsigned line, const DriverOptions &options,
                         bool show) {
  if (!options.compilation_database.empty())
    throw std::runtime_error("inspection requires explicit Clang arguments rather than a compilation database");
  const auto source = fs::weakly_canonical(fs::absolute(source_path));
  auto arguments = options.clang_arguments;
  arguments.push_back("-DASCIICHAT_DEFER_PARSING=1");
  std::vector<const char *> raw;
  for (const auto &arg : arguments)
    raw.push_back(arg.c_str());
  auto content = read_file(source);
  const auto source_name = source.u8string();
  auto unit = parse_wrapped(index.get(), source_name, raw, content);
  validate_generated(index.get(), source_name, raw, transform(unit->get(), source_name, content));
  return show ? show_transformed(unit->get(), source_name, content, line)
              : explain(unit->get(), source_name, content, line);
}

void write_depfile(const fs::path &path, const std::vector<FileDependencies> &files) {
  if (files.empty())
    throw std::runtime_error("cannot write dependencies for an empty batch");
  std::set<std::string> all_inputs;
  for (const auto &file : files)
    all_inputs.insert(file.inputs.begin(), file.inputs.end());
  std::string result = escape_depfile(files.front().output.u8string()) + ":";
  for (const auto &input : all_inputs)
    result += " " + escape_depfile(input);
  result += "\n";
  write_if_changed(path, result);
}

} // namespace defer_tool
