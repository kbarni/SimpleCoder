#include "agent/Tool.hpp"

#include <algorithm>
#include <filesystem>
#include <vector>

namespace llmcli {

using nlohmann::json;
namespace fs = std::filesystem;

json ListDirTool::schema() const {
  return {
      {"type", "function"},
      {"function",
       {{"name", name()},
        {"description", "List the entries of a directory."},
        {"parameters",
         {{"type", "object"},
          {"properties",
           {{"path",
             {{"type", "string"},
              {"description", "Path to the directory to list."}}}}},
          {"required", json::array({"path"})}}}}}};
}

ToolResult ListDirTool::execute(const json& args) const {
  const std::string path = args.value("path", "");
  if (path.empty()) {
    return {false, "error: 'path' argument is required"};
  }

  std::error_code ec;
  if (!fs::exists(path, ec)) {
    return {false, "error: '" + path + "' does not exist"};
  }
  if (!fs::is_directory(path, ec)) {
    return {false, "error: '" + path + "' is not a directory"};
  }

  std::vector<std::string> lines;
  for (const auto& entry : fs::directory_iterator(path, ec)) {
    if (ec) break;
    const bool dir = entry.is_directory(ec);
    std::string line = dir ? "[dir]  " : "[file] ";
    line += entry.path().filename().string();
    if (!dir) {
      std::error_code se;
      const auto size = entry.file_size(se);
      if (!se) line += " (" + std::to_string(size) + " bytes)";
    }
    lines.push_back(std::move(line));
  }
  std::sort(lines.begin(), lines.end());

  if (lines.empty()) return {true, "(empty directory)"};

  std::string out;
  for (const auto& l : lines) out += l + "\n";
  return {true, out};
}

}  // namespace llmcli
