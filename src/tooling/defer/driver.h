#pragma once

#include "clang_support.h"
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace defer_tool {

struct DriverOptions {
  std::filesystem::path compilation_database;
  std::vector<std::string> clang_arguments;
};

struct FileDependencies {
  std::filesystem::path output;
  std::set<std::string> inputs;
};

std::string read_file(const std::filesystem::path &path);
void write_if_changed(const std::filesystem::path &path, const std::string &contents);
void verify_libclang(const std::filesystem::path &requested);
FileDependencies transform_file(ClangIndex &index, const std::filesystem::path &source,
                                const std::filesystem::path &output, const DriverOptions &options);
std::string inspect_file(ClangIndex &index, const std::filesystem::path &source, unsigned line,
                         const DriverOptions &options, bool show_transformed);
void write_depfile(const std::filesystem::path &path, const std::vector<FileDependencies> &files);

} // namespace defer_tool
