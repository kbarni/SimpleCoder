#include "agent/Tool.hpp"

#include <fstream>
#include <sstream>
#include <string>

#include "util/Diff.hpp"

namespace llmcli {

using nlohmann::json;

json StrReplaceTool::schema() const {
  return {
      {"type", "function"},
      {"function",
       {{"name", name()},
        {"description",
         "Replace an exact, unique substring in a file with new text. The "
         "old text must appear exactly once."},
        {"parameters",
         {{"type", "object"},
          {"properties",
           {{"path",
             {{"type", "string"}, {"description", "Path to the file to edit."}}},
            {"old_str",
             {{"type", "string"},
              {"description", "Exact text to replace (must be unique)."}}},
            {"new_str",
             {{"type", "string"}, {"description", "Replacement text."}}}}},
          {"required", json::array({"path", "old_str", "new_str"})}}}}}};
}

ToolResult StrReplaceTool::execute(const json& args) const {
  const std::string path = args.value("path", "");
  if (path.empty()) return {false, "error: 'path' argument is required"};
  if (!args.contains("old_str")) {
    return {false, "error: 'old_str' argument is required"};
  }
  const std::string old_str = args.value("old_str", "");
  const std::string new_str = args.value("new_str", "");
  if (old_str.empty()) {
    return {false, "error: 'old_str' must not be empty"};
  }

  std::string content;
  {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {false, "error: could not open '" + path + "'"};
    std::ostringstream ss;
    ss << in.rdbuf();
    content = ss.str();
  }

  const auto first = content.find(old_str);
  if (first == std::string::npos) {
    return {false, "error: old_str not found in '" + path + "'"};
  }
  if (content.find(old_str, first + old_str.size()) != std::string::npos) {
    return {false,
            "error: old_str is not unique in '" + path +
                "'; include more surrounding context"};
  }

  content.replace(first, old_str.size(), new_str);

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return {false, "error: could not open '" + path + "' for writing"};
  out.write(content.data(), static_cast<std::streamsize>(content.size()));
  if (!out) return {false, "error: failed while writing '" + path + "'"};

  return {true, "replaced 1 occurrence in " + path};
}

std::string StrReplaceTool::confirm_details(const json& args) const {
  const std::string path = args.value("path", "");
  const std::string old_str = args.value("old_str", "");
  const std::string new_str = args.value("new_str", "");
  const std::string header = "path: " + path;

  std::string current;
  {
    std::ifstream in(path, std::ios::binary);
    if (!in) return header + "\n(cannot read file — edit will fail)";
    std::ostringstream ss;
    ss << in.rdbuf();
    current = ss.str();
  }

  // Mirror execute()'s find/uniqueness checks so the preview matches reality.
  if (old_str.empty()) return header + "\n(old_str is empty — edit will fail)";
  const auto first = current.find(old_str);
  if (first == std::string::npos)
    return header + "\n(old_str not found — edit will fail)";
  if (current.find(old_str, first + old_str.size()) != std::string::npos)
    return header + "\n(old_str is not unique — edit will fail)";

  std::string updated = current;
  updated.replace(first, old_str.size(), new_str);
  const std::string diff = unified_diff(current, updated);
  if (diff.empty()) return header + "\n(no changes)";
  return header + "\n" + diff;
}

}  // namespace llmcli
