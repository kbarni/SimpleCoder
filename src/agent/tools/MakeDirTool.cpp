#include "agent/Tool.hpp"

#include <filesystem>

namespace llmcli {

using nlohmann::json;
namespace fs = std::filesystem;

json MakeDirTool::schema() const {
  return {
      {"type", "function"},
      {"function",
       {{"name", name()},
        {"description", "Create a directory, including any missing parents."},
        {"parameters",
         {{"type", "object"},
          {"properties",
           {{"path",
             {{"type", "string"},
              {"description", "Directory path to create."}}}}},
          {"required", json::array({"path"})}}}}}};
}

ToolResult MakeDirTool::execute(const json& args) const {
  const std::string path = args.value("path", "");
  if (path.empty()) return {false, "error: 'path' argument is required"};

  std::error_code ec;
  if (fs::exists(path, ec)) {
    if (fs::is_directory(path, ec)) {
      return {true, "directory already exists: " + path};
    }
    return {false, "error: '" + path + "' exists and is not a directory"};
  }

  fs::create_directories(path, ec);
  if (ec) {
    return {false, "error: could not create '" + path + "': " + ec.message()};
  }
  return {true, "created directory " + path};
}

}  // namespace llmcli
