#pragma once

#include <filesystem>
#include <string>

namespace defer_tool {
void plan_batches(const std::filesystem::path &records, const std::filesystem::path &build_directory,
                  const std::string &stage, const std::filesystem::path &output);
} // namespace defer_tool
