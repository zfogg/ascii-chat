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
#include <filesystem>
#include <atomic>
#include <algorithm>
#include <exception>
#include <fstream>
#include <iterator>
#include <limits>
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
llvm::cl::opt<std::string> batch("batch", llvm::cl::desc("Transform source/output pairs from a manifest"));
llvm::cl::opt<unsigned> jobs("jobs", llvm::cl::desc("Maximum concurrent transformations (default: available cores)"),
                             llvm::cl::init(0));
llvm::cl::opt<std::string> libclang("libclang", llvm::cl::desc("Path to the libclang shared library"));
llvm::cl::opt<std::string> explain_location("explain", llvm::cl::desc("Explain cleanup at source:line"));
llvm::cl::opt<std::string> show_location("show-transformed",
                                         llvm::cl::desc("Show the transformed function at source:line"));

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
    for (const auto &[source, output] : inputs) {
      for (const auto &[other_source, other_output] : inputs) {
        std::error_code error;
        if (std::filesystem::weakly_canonical(output) == std::filesystem::weakly_canonical(other_source) ||
            (std::filesystem::exists(output) && std::filesystem::exists(other_source) &&
             std::filesystem::equivalent(output, other_source, error) && !error))
          throw std::runtime_error("output must not overwrite an input source");
      }
    }
    for (std::size_t i = 0; i < inputs.size(); ++i)
      for (std::size_t j = 0; j < i; ++j) {
        std::error_code error;
        if (std::filesystem::weakly_canonical(inputs[i].second) ==
                std::filesystem::weakly_canonical(inputs[j].second) ||
            (std::filesystem::exists(inputs[i].second) && std::filesystem::exists(inputs[j].second) &&
             std::filesystem::equivalent(inputs[i].second, inputs[j].second, error) && !error))
          throw std::runtime_error("batch output paths must be unique");
      }
    if (!depfile_path.empty()) {
      for (const auto &[source, output] : inputs) {
        for (const auto &protected_path : {source, output}) {
          std::error_code error;
          if (std::filesystem::weakly_canonical(depfile_path) == std::filesystem::weakly_canonical(protected_path) ||
              (std::filesystem::exists(depfile_path) && std::filesystem::exists(protected_path) &&
               std::filesystem::equivalent(depfile_path, protected_path, error) && !error))
            throw std::runtime_error("depfile must not overwrite an input source or output");
        }
      }
    }
    if (dependency_stamps) {
      std::set<std::filesystem::path> stamps;
      for (const auto &[source, output] : inputs) {
        const auto stamp = std::filesystem::u8path(output.parent_path().u8string() + ".deps");
        if (!stamps.insert(std::filesystem::weakly_canonical(stamp)).second)
          throw std::runtime_error("dependency stamp paths must be unique");
        if (!depfile_path.empty() &&
            std::filesystem::weakly_canonical(stamp) == std::filesystem::weakly_canonical(depfile_path))
          throw std::runtime_error("dependency stamp must not overwrite the depfile");
        for (const auto &[protected_source, protected_output] : inputs) {
          for (const auto &protected_path : {protected_source, protected_output}) {
            std::error_code error;
            if (std::filesystem::weakly_canonical(stamp) == std::filesystem::weakly_canonical(protected_path) ||
                (std::filesystem::exists(stamp) && std::filesystem::exists(protected_path) &&
                 std::filesystem::equivalent(stamp, protected_path, error) && !error))
              throw std::runtime_error("dependency stamp must not overwrite an input source or output");
          }
        }
      }
    }
    if (jobs.getNumOccurrences() && jobs == 0)
      throw std::runtime_error("--jobs must be at least 1");
    dependencies.resize(inputs.size());
    std::vector<std::exception_ptr> errors(inputs.size());
    std::atomic<std::size_t> next{0};
    const auto worker = [&]() {
      try {
        defer_tool::ClangIndex worker_index;
        while (true) {
          const auto i = next.fetch_add(1);
          if (i >= inputs.size())
            break;
          try {
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
      for (const auto &file : dependencies)
        defer_tool::write_dependency_stamp(file);
    if (!depfile_path.empty())
      defer_tool::write_depfile(depfile_path, dependencies);
    return 0;
  } catch (const std::exception &error) {
    llvm::errs() << "defer-tool: " << error.what() << '\n';
    return 1;
  }
}
