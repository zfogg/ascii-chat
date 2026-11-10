#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace defer_tool {
struct ManifestRecord {
  std::filesystem::path source;
  std::filesystem::path output;
  std::vector<std::string> arguments;
};

class Manifest {
public:
  static Manifest parse(std::string_view text, bool compilation_database);
  const std::vector<ManifestRecord> &records() const {
    return records_;
  }

private:
  std::vector<ManifestRecord> records_;
};
} // namespace defer_tool
