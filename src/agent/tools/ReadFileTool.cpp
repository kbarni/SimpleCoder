#include "agent/Tool.hpp"

#include <fstream>
#include <sstream>

namespace llmcli {

using nlohmann::json;

json ReadFileTool::schema() const {
  return {
      {"type", "function"},
      {"function",
       {{"name", name()},
        {"description", "Read and return the contents of a text file."},
        {"parameters",
         {{"type", "object"},
          {"properties",
           {{"path",
             {{"type", "string"},
              {"description", "Path to the file to read."}}}}},
          {"required", json::array({"path"})}}}}}};
}

ToolResult ReadFileTool::execute(const json& args) const {
  const std::string path = args.value("path", "");
  if (path.empty()) {
    return {false, "error: 'path' argument is required"};
  }

  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {false, "error: could not open '" + path + "'"};
  }

  std::ostringstream ss;
  ss << in.rdbuf();
  return {true, ss.str()};
}

}  // namespace llmcli
