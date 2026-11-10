#include "manifest.h"

#include <llvm/Support/JSON.h>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace defer_tool {
Manifest Manifest::parse(std::string_view text, bool compilation_database) {
  Manifest manifest;
  std::istringstream lines{std::string(text)};
  std::string line;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    const auto first = line.find('\t');
    const auto third = first == std::string::npos ? std::string::npos : line.find('\t', first + 1);
    if (first == std::string::npos || first == 0 ||
        (third != std::string::npos && line.find('\t', third + 1) != std::string::npos))
      throw std::runtime_error("invalid source/output batch record");
    const auto output = line.substr(first + 1, third == std::string::npos ? third : third - first - 1);
    if (output.empty())
      throw std::runtime_error("invalid source/output batch record");
    ManifestRecord record{std::filesystem::u8path(line.substr(0, first)), std::filesystem::u8path(output), {}};
    if (third != std::string::npos) {
      if (compilation_database)
        throw std::runtime_error("per-file arguments cannot be combined with --compilation-database");
      auto parsed = llvm::json::parse(line.substr(third + 1));
      if (!parsed) {
        llvm::consumeError(parsed.takeError());
        throw std::runtime_error("batch arguments must be a JSON array of strings");
      }
      const auto *array = parsed->getAsArray();
      if (!array)
        throw std::runtime_error("batch arguments must be a JSON array of strings");
      for (const auto &item : *array) {
        const auto value = item.getAsString();
        if (!value || value->contains('\0'))
          throw std::runtime_error("batch arguments must be a JSON array of strings without NUL bytes");
        record.arguments.push_back(value->str());
      }
    }
    manifest.records_.push_back(std::move(record));
  }
  return manifest;
}
} // namespace defer_tool
