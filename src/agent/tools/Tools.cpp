#include "agent/Tool.hpp"

namespace llmcli {

using nlohmann::json;

std::string Tool::confirm_details(const json& args) const {
  if (args.contains("command") && args["command"].is_string()) {
    return args["command"].get<std::string>();
  }
  if (args.contains("path") && args["path"].is_string()) {
    std::string d = "path: " + args["path"].get<std::string>();
    if (args.contains("content") && args["content"].is_string()) {
      d += " (" + std::to_string(args["content"].get<std::string>().size()) +
           " bytes)";
    }
    return d;
  }
  return args.dump();
}

std::vector<std::unique_ptr<Tool>> default_tools(const WebToolsConfig& web) {
  std::vector<std::unique_ptr<Tool>> tools;
  tools.push_back(std::make_unique<ReadFileTool>());
  tools.push_back(std::make_unique<WriteFileTool>());
  tools.push_back(std::make_unique<RunBashTool>());
  tools.push_back(std::make_unique<ListDirTool>());
  tools.push_back(std::make_unique<GrepSearchTool>());
  tools.push_back(std::make_unique<StrReplaceTool>());
  tools.push_back(std::make_unique<MakeDirTool>());
  // Web access is opt-in (outbound network egress); omit the tools entirely
  // when disabled so the model never sees them.
  if (web.enabled) {
    tools.push_back(std::make_unique<FetchUrlTool>());
    tools.push_back(std::make_unique<WebSearchTool>(web));
  }
  return tools;
}

}  // namespace llmcli
