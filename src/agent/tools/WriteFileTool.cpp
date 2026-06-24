#include "agent/Tool.hpp"

#include <fstream>
#include <sstream>

#include "util/Diff.hpp"

namespace llmcli {

using nlohmann::json;

json WriteFileTool::schema() const {
  return {
      {"type", "function"},
      {"function",
       {{"name", name()},
        {"description",
         "Create or overwrite a file with the given text content."},
        {"parameters",
         {{"type", "object"},
          {"properties",
           {{"path",
             {{"type", "string"},
              {"description", "Path to the file to write."}}},
            {"content",
             {{"type", "string"},
              {"description", "Full text content to write to the file."}}}}},
          {"required", json::array({"path", "content"})}}}}}};
}

ToolResult WriteFileTool::execute(const json& args) const {
  const std::string path = args.value("path", "");
  if (path.empty()) {
    return {false, "error: 'path' argument is required"};
  }
  const std::string content = args.value("content", "");

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return {false, "error: could not open '" + path + "' for writing"};
  }
  out.write(content.data(), static_cast<std::streamsize>(content.size()));
  if (!out) {
    return {false, "error: failed while writing '" + path + "'"};
  }

  return {true, "wrote " + std::to_string(content.size()) + " bytes to " + path};
}

std::string WriteFileTool::confirm_details(const json& args) const {
  const std::string path = args.value("path", "");
  const std::string content = args.value("content", "");

  std::string current;
  bool exists = false;
  {
    std::ifstream in(path, std::ios::binary);
    if (in) {
      exists = true;
      std::ostringstream ss;
      ss << in.rdbuf();
      current = ss.str();
    }
  }

  const std::string header =
      "path: " + path + (exists ? " (overwrite, " : " (new file, ") +
      std::to_string(content.size()) + " bytes)";
  const std::string diff = unified_diff(current, content);
  if (diff.empty()) return header + "\n(no changes)";
  return header + "\n" + diff;
}

}  // namespace llmcli
