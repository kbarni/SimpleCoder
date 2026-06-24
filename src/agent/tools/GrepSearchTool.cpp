#include "agent/Tool.hpp"

#include <filesystem>
#include <fstream>
#include <regex>
#include <string>

namespace llmcli {

using nlohmann::json;
namespace fs = std::filesystem;

namespace {

constexpr int kMaxMatches = 500;
constexpr std::size_t kMaxLineLen = 300;

// Append matching lines from one file to `out`; returns matches added.
int search_file(const fs::path& file, const std::regex& re, std::string& out,
                int budget) {
  std::ifstream in(file);
  if (!in) return 0;

  int added = 0;
  std::string line;
  int lineno = 0;
  while (added < budget && std::getline(in, line)) {
    ++lineno;
    if (std::regex_search(line, re)) {
      if (line.size() > kMaxLineLen) line = line.substr(0, kMaxLineLen) + "…";
      out += file.string() + ":" + std::to_string(lineno) + ": " + line + "\n";
      ++added;
    }
  }
  return added;
}

}  // namespace

json GrepSearchTool::schema() const {
  return {
      {"type", "function"},
      {"function",
       {{"name", name()},
        {"description",
         "Search for a regular expression in a file, or recursively under a "
         "directory. Returns matching lines as path:line: text."},
        {"parameters",
         {{"type", "object"},
          {"properties",
           {{"pattern",
             {{"type", "string"},
              {"description", "ECMAScript regular expression to search for."}}},
            {"path",
             {{"type", "string"},
              {"description", "File or directory to search."}}}}},
          {"required", json::array({"pattern", "path"})}}}}}};
}

ToolResult GrepSearchTool::execute(const json& args) const {
  const std::string pattern = args.value("pattern", "");
  const std::string path = args.value("path", "");
  if (pattern.empty()) return {false, "error: 'pattern' argument is required"};
  if (path.empty()) return {false, "error: 'path' argument is required"};

  std::regex re;
  try {
    re = std::regex(pattern);
  } catch (const std::regex_error& e) {
    return {false, "error: invalid regex: " + std::string(e.what())};
  }

  std::error_code ec;
  if (!fs::exists(path, ec)) {
    return {false, "error: '" + path + "' does not exist"};
  }

  std::string out;
  int remaining = kMaxMatches;
  if (fs::is_directory(path, ec)) {
    for (const auto& entry : fs::recursive_directory_iterator(
             path, fs::directory_options::skip_permission_denied, ec)) {
      if (remaining <= 0) break;
      if (entry.is_regular_file(ec)) {
        remaining -= search_file(entry.path(), re, out, remaining);
      }
    }
  } else {
    remaining -= search_file(path, re, out, remaining);
  }

  if (out.empty()) return {true, "(no matches)"};
  if (remaining <= 0) out += "[truncated at " + std::to_string(kMaxMatches) +
                             " matches]\n";
  return {true, out};
}

}  // namespace llmcli
