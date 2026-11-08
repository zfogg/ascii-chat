#include "plan.h"
#include "driver.h"

#include <llvm/ADT/StringExtras.h>
#include <llvm/Support/SHA256.h>
#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace defer_tool {
namespace fs = std::filesystem;
namespace {
struct Record {
  std::string target;
  std::string source;
  std::string output;
};

std::string literal(const std::string &value) {
  std::string separator = "=";
  while (value.find("]" + separator + "]") != std::string::npos) separator += "=";
  return "[" + separator + "[" + value + "]" + separator + "]";
}

std::vector<std::string> split(const std::string &value, char delimiter) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (true) {
    const auto end = value.find(delimiter, start);
    parts.push_back(value.substr(start, end == std::string::npos ? end : end - start));
    if (end == std::string::npos) return parts;
    start = end + 1;
  }
}
} // namespace

void plan_batches(const fs::path &records, const fs::path &build_path, const std::string &stage,
                  const fs::path &output) {
  const auto build_directory = fs::absolute(build_path).lexically_normal();
  std::map<std::vector<std::string>, std::vector<Record>> batches;
  std::istringstream lines(read_file(records));
  std::string line;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto fields = split(line, '\t');
    if (fields.size() != 4) throw std::runtime_error("invalid defer planning record");
    auto prerequisites = split(fields[3], ';');
    prerequisites.erase(std::remove(prerequisites.begin(), prerequisites.end(), ""), prerequisites.end());
    std::sort(prerequisites.begin(), prerequisites.end());
    prerequisites.erase(std::unique(prerequisites.begin(), prerequisites.end()), prerequisites.end());
    batches[prerequisites].push_back({fields[0], fields[1], fields[2]});
  }
  std::string rules;
  for (const auto &[prerequisites, entries] : batches) {
    llvm::SHA256 hash;
    hash.update(stage);
    for (const auto &entry : prerequisites) { hash.update("\n"); hash.update(entry); }
    auto digest = hash.final();
    const auto id = llvm::toHex(digest, true).substr(0, 24);
    const auto name = literal("ascii-defer-" + id);
    const auto manifest = build_directory / "defer_transformed" / ("batch-" + id + ".txt");
    const auto depfile = build_directory / "defer_transformed" / ("batch-" + id + ".d");
    std::string content, sources, outputs;
    std::set<std::string> targets;
    for (const auto &entry : entries) {
      content += entry.source + "\t" + entry.output + "\n";
      sources += " " + literal(entry.source);
      outputs += " " + literal(entry.output);
      targets.insert(entry.target);
    }
    write_if_changed(manifest, content);
    rules += "add_custom_command(\n  OUTPUT" + outputs + "\n";
    rules += "  COMMAND \"${ASCIICHAT_DEFER_NATIVE_BINARY}\" --batch " + literal(manifest.generic_u8string()) +
             " --compilation-database \"${CMAKE_BINARY_DIR}\" --depfile " + literal(depfile.generic_u8string()) + "\n";
    rules += "  DEPENDS" + sources + " " + literal(manifest.generic_u8string()) +
             " \"${ASCIICHAT_DEFER_NATIVE_BINARY}\" \"${CMAKE_BINARY_DIR}/compile_commands.json\"\n";
    rules += "  DEPFILE " + literal(depfile.generic_u8string()) +
             "\n  COMMENT \"Transforming defer source batch\" VERBATIM)\n";
    rules += "add_custom_target(" + name + " DEPENDS" + outputs + ")\n";
    if (!prerequisites.empty()) {
      rules += "add_dependencies(" + name;
      for (const auto &dependency : prerequisites) rules += " " + literal(dependency);
      rules += ")\n";
    }
    for (const auto &target : targets) rules += "add_dependencies(" + literal(target) + " " + name + ")\n";
  }
  write_if_changed(output, rules);
}
} // namespace defer_tool
