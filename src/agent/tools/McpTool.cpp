#include "agent/Tool.hpp"

#include <utility>

#include "mcp/McpClient.hpp"

namespace llmcli {

using nlohmann::json;

McpTool::McpTool(std::shared_ptr<mcp::McpClient> client, std::string exposed_name,
                 std::string remote_name, std::string description,
                 json input_schema)
    : client_(std::move(client)),
      exposed_name_(std::move(exposed_name)),
      remote_name_(std::move(remote_name)),
      description_(std::move(description)),
      input_schema_(std::move(input_schema)) {}

json McpTool::schema() const {
  // The server's inputSchema is already a JSON Schema object — exactly what goes
  // under "parameters". Fall back to an empty object schema if it had none.
  json params = input_schema_.is_object()
                    ? input_schema_
                    : json{{"type", "object"}, {"properties", json::object()}};
  return {{"type", "function"},
          {"function",
           {{"name", exposed_name_},
            {"description", description_},
            {"parameters", std::move(params)}}}};
}

ToolResult McpTool::execute(const json& args) const {
  mcp::McpCallResult r = client_->call(remote_name_, args);
  return {r.ok, std::move(r.text)};
}

}  // namespace llmcli
