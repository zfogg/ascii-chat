#include "clang_support.h"
#include "driver.h"
#include "plan.h"
#include "text.h"
#include "transform.h"
#include <clang-c/Index.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/Process.h>
#include <llvm/Support/Threading.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Support/SHA256.h>
#include <filesystem>
#include <atomic>
#include <algorithm>
#include <exception>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
llvm::cl::opt<bool> scan_only("scan", llvm::cl::desc("List source files containing possible defer calls"));
llvm::cl::opt<std::string> scan_input("scan-input", llvm::cl::desc("Read source paths from a UTF-8 file"));
llvm::cl::opt<std::string> plan_records("plan-records", llvm::cl::desc("Plan source/output records"));
llvm::cl::opt<std::string> plan_stage("plan-stage", llvm::cl::desc("Build stage for batch planning"));
llvm::cl::opt<std::string> plan_output("plan-output", llvm::cl::desc("Write generated CMake build rules"));
llvm::cl::opt<std::string> build_directory("build-dir", llvm::cl::desc("Build directory for batch planning"));
llvm::cl::list<std::string> sources(llvm::cl::Positional, llvm::cl::desc("<source files>"), llvm::cl::ZeroOrMore);
llvm::cl::opt<std::string> compilation_database("compilation-database", llvm::cl::desc("CMake compilation database"));
llvm::cl::opt<std::string> depfile("depfile", llvm::cl::desc("Write header dependencies"));
llvm::cl::opt<bool> dependency_stamps("dependency-stamps", llvm::cl::desc("Write per-output dependency stamps"));
llvm::cl::opt<bool> incremental("incremental", llvm::cl::desc("Reuse unchanged batch transformations"));
llvm::cl::opt<bool> preamble("preamble", llvm::cl::desc("Experimentally reuse Clang preambles for validation"));
llvm::cl::opt<std::string> shared_cache("shared-cache", llvm::cl::desc("Directory for reusable transformation cache"));
llvm::cl::opt<std::string> batch("batch", llvm::cl::desc("Transform source/output pairs from a manifest"));
llvm::cl::opt<unsigned> jobs("jobs", llvm::cl::desc("Maximum concurrent transformations (default: available cores)"),
                             llvm::cl::init(0));
llvm::cl::opt<std::string> libclang("libclang", llvm::cl::desc("Path to the libclang shared library"));
llvm::cl::opt<std::string> explain_location("explain", llvm::cl::desc("Explain cleanup at source:line"));
llvm::cl::opt<std::string> show_location("show-transformed",
                                         llvm::cl::desc("Show the transformed function at source:line"));

using FileMetadata = std::pair<long long, std::uintmax_t>;

std::string digest(const std::string &value) {
  llvm::SHA256 hash;
  hash.update(llvm::StringRef(value));
  return llvm::toHex(hash.final(), true);
}

std::string normalize_build_path(std::string value, const std::string &build) {
  if (build.empty())
    return value;
  auto alternate = build;
  std::replace(alternate.begin(), alternate.end(), '/', '\\');
  for (const auto &form : {build, alternate}) {
    for (std::size_t pos = 0; (pos = value.find(form, pos)) != std::string::npos;) {
      const auto end = pos + form.size();
      if (end == value.size() || value[end] == '/' || value[end] == '\\') {
        value.replace(pos, form.size(), "@DEFER_BUILD@");
        pos += 13;
      } else {
        pos = end;
      }
    }
  }
  return value;
}

std::string denormalize_build_path(std::string value, const std::string &build) {
  const std::string marker = "@DEFER_BUILD@";
  for (std::size_t pos = 0; (pos = value.find(marker, pos)) != std::string::npos; pos += build.size())
    value.replace(pos, marker.size(), build);
  return value;
}

std::string file_digest(const std::filesystem::path &path, std::map<std::string, std::string> &known) {
  static std::mutex digest_mutex;
  const std::lock_guard<std::mutex> lock(digest_mutex);
  const auto name = std::filesystem::absolute(path).lexically_normal().u8string();
  auto found = known.find(name);
  if (found != known.end())
    return found->second;
  const auto contents = defer_tool::read_file(path);
  const bool volatile_macro =
      contents.find("__DATE__") != std::string::npos || contents.find("__TIME__") != std::string::npos ||
      contents.find("__TIMESTAMP__") != std::string::npos || contents.find("__has_include") != std::string::npos ||
      contents.find("#embed") != std::string::npos;
  return known.emplace(name, (volatile_macro ? "volatile:" : "") + digest(contents)).first->second;
}

bool restore_shared(const std::filesystem::path &root, const std::string &key, const std::string &build,
                    const std::filesystem::path &source, const std::filesystem::path &output,
                    const std::vector<std::string> &arguments, std::map<std::string, std::string> &known,
                    defer_tool::ClangIndex &index, defer_tool::FileDependencies &result) {
  namespace fs = std::filesystem;
  try {
    std::istringstream lines(defer_tool::read_file(root / (key + ".deps")));
    std::string expected_output, line;
    if (!std::getline(lines, expected_output))
      return false;
    result = {output, {}};
    while (std::getline(lines, line)) {
      const auto separator = line.rfind('\t');
      if (separator == std::string::npos)
        return false;
      const auto name = denormalize_build_path(line.substr(0, separator), build);
      const auto actual = file_digest(fs::u8path(name), known);
      if (actual.rfind("volatile:", 0) == 0 || actual != line.substr(separator + 1))
        return false;
      result.inputs.insert(name);
    }
    if (!result.inputs.count(fs::weakly_canonical(fs::absolute(source)).u8string()))
      return false;
    auto parse_arguments = arguments;
    parse_arguments.push_back("-DASCIICHAT_DEFER_PARSING=1");
    std::vector<const char *> raw;
    for (const auto &argument : parse_arguments)
      raw.push_back(argument.c_str());
    const auto source_name = fs::weakly_canonical(fs::absolute(source)).u8string();
    defer_tool::TranslationUnit unit(index.get(), source_name, raw, nullptr, 0, false, false, true);
    if (!unit.errors().empty())
      return false;
    std::set<std::string> current{source_name};
    clang_getInclusions(
        unit.get(),
        [](CXFile file, CXSourceLocation *, unsigned, CXClientData opaque) {
          auto *names = static_cast<std::set<std::string> *>(opaque);
          names->insert(fs::absolute(fs::u8path(defer_tool::consume_string(clang_getFileName(file))))
                            .lexically_normal()
                            .u8string());
        },
        &current);
    std::set<fs::path> expected_paths, current_paths;
    for (const auto &name : result.inputs)
      expected_paths.insert(fs::weakly_canonical(fs::u8path(name)));
    for (const auto &name : current)
      current_paths.insert(fs::weakly_canonical(fs::u8path(name)));
    if (current_paths != expected_paths)
      return false;
    result.inputs = std::move(current);
    const auto content = defer_tool::read_file(root / (key + ".c"));
    if (digest(content) != expected_output)
      return false;
    defer_tool::write_if_changed(output, content);
    return true;
  } catch (const std::exception &) {
    return false;
  }
}

void save_shared(const std::filesystem::path &root, const std::string &key, const std::string &build,
                 const defer_tool::FileDependencies &file, std::map<std::string, std::string> &known) {
  try {
    const auto content = defer_tool::read_file(file.output);
    std::string manifest = digest(content) + "\n";
    for (const auto &name : file.inputs) {
      const auto hash = file_digest(std::filesystem::u8path(name), known);
      if (hash.rfind("volatile:", 0) == 0)
        return;
      manifest += normalize_build_path(name, build) + "\t" + hash + "\n";
    }
    defer_tool::write_if_changed(root / (key + ".c"), content);
    defer_tool::write_if_changed(root / (key + ".deps"), manifest);
  } catch (const std::exception &) {
    // Shared cache failures must not prevent a valid local build.
  }
}

bool metadata(const std::filesystem::path &path, FileMetadata &result) {
  std::error_code error;
  const auto modified = std::filesystem::last_write_time(path, error);
  if (error)
    return false;
  const auto size = std::filesystem::file_size(path, error);
  if (error)
    return false;
  result = {static_cast<long long>(modified.time_since_epoch().count()), size};
  return true;
}

std::string cache_context(const char *program) {
  std::string context = "defer-cache-v2\n";
#ifdef ASCII_DEFER_BUILD_ID
  (void)program;
  context += ASCII_DEFER_BUILD_ID "\n";
#else
  try {
    context += digest(defer_tool::read_file(std::filesystem::u8path(program))) + "\n";
  } catch (const std::exception &) {
    return {};
  }
#endif
  context += defer_tool::consume_string(clang_getClangVersion()) + "\n";
  for (const char *name :
       {"ASCIICHAT_DEFER_EXTRA_INCLUDE_DIRS", "CPATH", "C_INCLUDE_PATH", "SDKROOT", "INCLUDE", "PATH"})
    context += std::string(name) + "=" + llvm::sys::Process::GetEnv(name).value_or("") + "\n";
  return context;
}

bool cached_dependencies(const std::filesystem::path &source, const std::filesystem::path &output,
                         const std::string &context, std::map<std::string, FileMetadata> &metadata_cache,
                         defer_tool::FileDependencies &result) {
  namespace fs = std::filesystem;
  const auto stamp = fs::u8path(output.parent_path().u8string() + ".deps");
  const auto cache = fs::u8path(output.parent_path().u8string() + ".cache");
  try {
    if (context.empty() || defer_tool::read_file(cache) != context || !fs::is_regular_file(output) ||
        fs::last_write_time(output) > fs::last_write_time(stamp))
      return false;
    std::istringstream lines(defer_tool::read_file(stamp));
    std::string line;
    result = {output, {}};
    while (std::getline(lines, line)) {
      const auto last = line.rfind('\t');
      const auto first = last == std::string::npos ? last : line.rfind('\t', last - 1);
      if (first == std::string::npos || last == std::string::npos)
        return false;
      const auto name = line.substr(0, first);
      const FileMetadata expected{std::stoll(line.substr(first + 1, last - first - 1)),
                                  std::stoull(line.substr(last + 1))};
      auto found = metadata_cache.find(name);
      if (found == metadata_cache.end()) {
        FileMetadata actual;
        if (!metadata(fs::u8path(name), actual))
          return false;
        found = metadata_cache.emplace(name, actual).first;
      }
      if (found->second != expected)
        return false;
      result.inputs.insert(name);
    }
    return result.inputs.count(fs::weakly_canonical(fs::absolute(source)).u8string()) != 0;
  } catch (const std::exception &) {
    return false;
  }
}

} // namespace

int main(int argc, char **argv) {
  llvm::InitLLVM initializer(argc, argv);
  llvm::cl::ParseCommandLineOptions(argc, argv, "ascii-chat defer source transformer\n");
  try {
    if (scan_only) {
      if (!scan_input.empty()) {
        std::istringstream lines(defer_tool::read_file(std::filesystem::u8path(scan_input.getValue())));
        std::string source;
        while (std::getline(lines, source)) {
          if (!source.empty() && source.back() == '\r')
            source.pop_back();
          if (!source.empty() && defer_tool::may_contain_defer(defer_tool::read_file(std::filesystem::u8path(source))))
            llvm::outs() << source << '\n';
        }
      }
      for (const auto &source : sources)
        if (defer_tool::may_contain_defer(defer_tool::read_file(std::filesystem::u8path(source))))
          llvm::outs() << source << '\n';
      return 0;
    }
    if (!plan_records.empty()) {
      if (plan_stage.empty() || plan_output.empty() || build_directory.empty())
        throw std::runtime_error("planning requires --plan-stage, --plan-output, and --build-dir");
      defer_tool::plan_batches(std::filesystem::u8path(plan_records.getValue()),
                               std::filesystem::u8path(build_directory.getValue()), plan_stage.getValue(),
                               std::filesystem::u8path(plan_output.getValue()));
      return 0;
    }
    if (!libclang.empty())
      defer_tool::verify_libclang(std::filesystem::u8path(libclang.getValue()));
    defer_tool::DriverOptions options;
    options.preamble = preamble;
    if (!compilation_database.empty())
      options.compilation_database =
          std::filesystem::absolute(std::filesystem::u8path(compilation_database.getValue()));
    const auto depfile_path = depfile.empty() ? std::filesystem::path{}
                                              : std::filesystem::absolute(std::filesystem::u8path(depfile.getValue()));
    defer_tool::ClangIndex index;
    if (!explain_location.empty() || !show_location.empty()) {
      if ((!explain_location.empty() && !show_location.empty()) || !batch.empty() || !depfile.empty())
        throw std::runtime_error("inspection modes cannot be combined with each other, --batch, or --depfile");
      const auto location = !show_location.empty() ? show_location.getValue() : explain_location.getValue();
      const auto separator = location.rfind(':');
      if (separator == std::string::npos || separator == 0 || separator + 1 == location.size() ||
          location.find_first_not_of("0123456789", separator + 1) != std::string::npos)
        throw std::runtime_error("--explain/--show-transformed expects source:line");
      const auto line = std::stoul(location.substr(separator + 1));
      if (!line || line > std::numeric_limits<unsigned>::max())
        throw std::runtime_error("--explain line must be a positive integer");
      options.clang_arguments.assign(sources.begin(), sources.end());
      llvm::outs() << defer_tool::inspect_file(index, std::filesystem::u8path(location.substr(0, separator)),
                                               static_cast<unsigned>(line), options, !show_location.empty());
      return 0;
    }
    std::vector<defer_tool::FileDependencies> dependencies;
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> inputs;
    if (!batch.empty()) {
      if (!sources.empty())
        throw std::runtime_error("batch mode does not accept positional source files");
      auto manifest = defer_tool::read_file(std::filesystem::absolute(std::filesystem::u8path(batch.getValue())));
      std::istringstream lines(manifest);
      std::string line;
      while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r')
          line.pop_back();
        auto separator = line.find('\t');
        if (separator == std::string::npos || line.find('\t', separator + 1) != std::string::npos)
          throw std::runtime_error("invalid source/output batch record");
        inputs.emplace_back(std::filesystem::u8path(line.substr(0, separator)),
                            std::filesystem::u8path(line.substr(separator + 1)));
      }
    } else {
      if (sources.size() < 2)
        throw std::runtime_error("expected source and output paths");
      options.clang_arguments.assign(sources.begin() + 2, sources.end());
      inputs.emplace_back(std::filesystem::u8path(sources[0]), std::filesystem::u8path(sources[1]));
    }
    struct PathIdentity {
      std::filesystem::path canonical;
      std::optional<llvm::sys::fs::UniqueID> id;
    };
    std::map<std::filesystem::path, PathIdentity> identities;
    const auto identify = [&](const std::filesystem::path &path) -> const PathIdentity & {
      auto found = identities.find(path);
      if (found != identities.end())
        return found->second;
      PathIdentity identity{std::filesystem::weakly_canonical(path), std::nullopt};
      llvm::sys::fs::UniqueID id;
      if (!llvm::sys::fs::getUniqueID(path.u8string(), id))
        identity.id = id;
      return identities.emplace(path, std::move(identity)).first->second;
    };
    const auto aliases = [&](const std::filesystem::path &a, const std::filesystem::path &b) {
      const auto &left = identify(a);
      const auto &right = identify(b);
      return left.canonical == right.canonical || (left.id && right.id && *left.id == *right.id);
    };
    for (const auto &[source, output] : inputs) {
      for (const auto &[other_source, other_output] : inputs) {
        if (aliases(output, other_source))
          throw std::runtime_error("output must not overwrite an input source");
      }
    }
    for (std::size_t i = 0; i < inputs.size(); ++i)
      for (std::size_t j = 0; j < i; ++j) {
        if (aliases(inputs[i].second, inputs[j].second))
          throw std::runtime_error("batch output paths must be unique");
      }
    if (!depfile_path.empty()) {
      for (const auto &[source, output] : inputs) {
        for (const auto &protected_path : {source, output}) {
          if (aliases(depfile_path, protected_path))
            throw std::runtime_error("depfile must not overwrite an input source or output");
        }
      }
    }
    if (dependency_stamps) {
      std::set<std::filesystem::path> stamps;
      for (const auto &[source, output] : inputs) {
        const auto stamp = std::filesystem::u8path(output.parent_path().u8string() + ".deps");
        if (!stamps.insert(identify(stamp).canonical).second)
          throw std::runtime_error("dependency stamp paths must be unique");
        if (!depfile_path.empty() && aliases(stamp, depfile_path))
          throw std::runtime_error("dependency stamp must not overwrite the depfile");
        for (const auto &[protected_source, protected_output] : inputs) {
          for (const auto &protected_path : {protected_source, protected_output}) {
            if (aliases(stamp, protected_path))
              throw std::runtime_error("dependency stamp must not overwrite an input source or output");
          }
        }
      }
    }
    if (jobs.getNumOccurrences() && jobs == 0)
      throw std::runtime_error("--jobs must be at least 1");
    if (incremental && (batch.empty() || !dependency_stamps || options.compilation_database.empty()))
      throw std::runtime_error("--incremental requires --batch, --dependency-stamps, and --compilation-database");
    if (!shared_cache.empty() && !incremental)
      throw std::runtime_error("--shared-cache requires --incremental");
    if (incremental) {
      for (const auto &[source, output] : inputs) {
        const auto cache = std::filesystem::u8path(output.parent_path().u8string() + ".cache");
        for (const auto &[other_source, other_output] : inputs) {
          const auto other_stamp = std::filesystem::u8path(other_output.parent_path().u8string() + ".deps");
          for (const auto &protected_path : {other_source, other_output, other_stamp, depfile_path}) {
            if (protected_path.empty())
              continue;
            if (aliases(cache, protected_path))
              throw std::runtime_error("incremental cache must not overwrite an input, output, stamp, or depfile");
          }
        }
      }
    }
    dependencies.resize(inputs.size());
    auto base_context = incremental ? cache_context(argv[0]) : std::string{};
    auto shared_base_context = base_context;
    if (!base_context.empty()) {
      try {
        const auto clang_path = defer_tool::loaded_libclang_path();
        FileMetadata info;
        if (!metadata(clang_path, info))
          throw std::runtime_error("cannot stat loaded libclang");
        base_context += std::to_string(info.first) + "\t" + std::to_string(info.second) + "\n";
        if (!shared_cache.empty())
          shared_base_context += digest(defer_tool::read_file(clang_path)) + "\n";
      } catch (const std::exception &) {
        base_context.clear();
        shared_base_context.clear();
      }
    }
    std::vector<std::string> contexts(inputs.size());
    std::vector<std::string> shared_keys(inputs.size());
    std::vector<std::vector<std::string>> file_arguments(inputs.size());
    std::vector<char> cached(inputs.size(), false);
    std::vector<char> restored(inputs.size(), false);
    std::map<std::string, FileMetadata> metadata_cache;
    std::map<std::string, std::string> content_digests;
    const auto build_path = options.compilation_database.generic_u8string();
    const auto shared_root = std::filesystem::u8path(shared_cache.getValue());
    if (incremental && !base_context.empty())
      for (std::size_t i = 0; i < inputs.size(); ++i) {
        contexts[i] = base_context + (options.preamble ? "preamble=1\n" : "preamble=0\n");
        auto shared_context = shared_base_context + (options.preamble ? "preamble=1\n" : "preamble=0\n");
        bool next_is_path = false;
        const auto arguments =
            defer_tool::compilation_arguments(options.compilation_database, inputs[i].second, inputs[i].first);
        file_arguments[i] = arguments;
        for (const auto &argument : arguments) {
          contexts[i] += std::to_string(argument.size()) + ":" + argument + "\n";
          const bool path_argument = next_is_path || argument.rfind("-I", 0) == 0 ||
                                     argument.rfind("-isystem=", 0) == 0 || argument.rfind("--sysroot=", 0) == 0;
          next_is_path = argument == "-I" || argument == "-isystem" || argument == "-iquote" ||
                         argument == "-idirafter" || argument == "-isysroot" || argument == "--sysroot" ||
                         argument == "-working-directory";
          const auto normalized = path_argument ? normalize_build_path(argument, build_path) : argument;
          shared_context += std::to_string(normalized.size()) + ":" + normalized + "\n";
        }
        cached[i] =
            cached_dependencies(inputs[i].first, inputs[i].second, contexts[i], metadata_cache, dependencies[i]);
        if (!shared_cache.empty()) {
          const auto source = std::filesystem::weakly_canonical(std::filesystem::absolute(inputs[i].first)).u8string();
          if (normalize_build_path(source, build_path) == source) {
            shared_keys[i] = digest(shared_context + source + "\n" + file_digest(inputs[i].first, content_digests));
          }
        }
      }
    std::vector<std::exception_ptr> errors(inputs.size());
    std::atomic<std::size_t> next{0};
    const auto worker = [&]() {
      try {
        defer_tool::ClangIndex worker_index;
        while (true) {
          const auto i = next.fetch_add(1);
          if (i >= inputs.size())
            break;
          if (cached[i])
            continue;
          try {
            if (!shared_keys[i].empty() &&
                restore_shared(shared_root, shared_keys[i], build_path, inputs[i].first, inputs[i].second,
                               file_arguments[i], content_digests, worker_index, dependencies[i])) {
              cached[i] = true;
              restored[i] = true;
              continue;
            }
            dependencies[i] = defer_tool::transform_file(worker_index, inputs[i].first, inputs[i].second, options);
          } catch (...) {
            errors[i] = std::current_exception();
          }
        }
      } catch (...) {
        const auto i = next.fetch_add(1);
        if (i < inputs.size())
          errors[i] = std::current_exception();
      }
    };
    const auto available = std::max(1u, llvm::hardware_concurrency().compute_thread_count());
    const auto count = std::min<std::size_t>(jobs ? jobs.getValue() : available, inputs.size());
    std::vector<std::thread> workers;
    workers.reserve(count);
    try {
      for (std::size_t i = 0; i < count; ++i)
        workers.emplace_back(worker);
    } catch (...) {
      for (auto &thread : workers)
        thread.join();
      throw;
    }
    for (auto &thread : workers)
      thread.join();
    for (const auto &error : errors)
      if (error)
        std::rethrow_exception(error);
    if (dependency_stamps)
      for (std::size_t i = 0; i < dependencies.size(); ++i)
        if (!cached[i] || restored[i]) {
          defer_tool::write_dependency_stamp(dependencies[i]);
          if (incremental && !contexts[i].empty()) {
            const auto cache = std::filesystem::u8path(inputs[i].second.parent_path().u8string() + ".cache");
            for (const auto &name : dependencies[i].inputs) {
              const auto input = std::filesystem::u8path(name);
              if (aliases(cache, input))
                throw std::runtime_error("incremental cache must not overwrite an included file");
            }
            defer_tool::write_if_changed(cache, contexts[i]);
          }
        }
    for (std::size_t i = 0; i < dependencies.size(); ++i) {
      if (restored[i] || shared_keys[i].empty())
        continue;
      const auto data = shared_root / (shared_keys[i] + ".c");
      const auto manifest = shared_root / (shared_keys[i] + ".deps");
      if (cached[i] && std::filesystem::exists(data) && std::filesystem::exists(manifest))
        continue;
      bool safe = true;
      for (const auto &name : dependencies[i].inputs)
        if (aliases(data, std::filesystem::u8path(name)) || aliases(manifest, std::filesystem::u8path(name)))
          safe = false;
      for (const auto &[source, output] : inputs) {
        const auto stamp = std::filesystem::u8path(output.parent_path().u8string() + ".deps");
        const auto local_cache = std::filesystem::u8path(output.parent_path().u8string() + ".cache");
        for (const auto &protected_path : {source, output, stamp, local_cache, depfile_path})
          if (!protected_path.empty() && (aliases(data, protected_path) || aliases(manifest, protected_path)))
            safe = false;
      }
      if (safe)
        save_shared(shared_root, shared_keys[i], build_path, dependencies[i], content_digests);
    }
    if (!depfile_path.empty())
      defer_tool::write_depfile(depfile_path, dependencies);
    if (incremental)
      llvm::errs() << "defer-tool: transformed " << std::count(cached.begin(), cached.end(), false) << " of "
                   << inputs.size() << " files (" << std::count(restored.begin(), restored.end(), true)
                   << " restored from shared cache)\n";
    return 0;
  } catch (const std::exception &error) {
    llvm::errs() << "defer-tool: " << error.what() << '\n';
    return 1;
  }
}
