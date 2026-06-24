#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "mcp/Transport.hpp"

namespace llmcli {
class Tool;  // agent/Tool.hpp
}

namespace llmcli::mcp {

// A tool advertised by an MCP server (`tools/list` entry). `input_schema` is the
// arguments' JSON Schema, which McpTool passes straight through as "parameters".
struct McpToolInfo {
  std::string name;
  std::string description;
  nlohmann::json input_schema;
};

// Result of a `tools/call`. `text` joins the response's text content blocks; `ok`
// is false on a JSON-RPC error or when the tool reports `isError`.
struct McpCallResult {
  bool ok = true;
  std::string text;
};

// A JSON-RPC client for one MCP server, driven through the Transport seam (so
// tests can script responses without a subprocess). Owns the transport, so
// destroying the client tears down the server.
class McpClient {
 public:
  explicit McpClient(TransportPtr transport, std::string name = "");

  // Run the `initialize` handshake, send `notifications/initialized`, then
  // `tools/list` (following pagination), caching the advertised tools. Returns
  // false and sets last_error() on any transport or protocol failure.
  bool start();

  // Invoke a remote tool. Never throws; failures land in McpCallResult.
  McpCallResult call(const std::string& tool, const nlohmann::json& args);

  const std::vector<McpToolInfo>& tools() const { return tools_; }
  const std::string& name() const { return name_; }
  const std::string& last_error() const { return last_error_; }

 private:
  int next_id() { return ++id_; }

  TransportPtr transport_;
  std::string name_;
  std::vector<McpToolInfo> tools_;
  std::string last_error_;
  int id_ = 0;
};

// Parse a sidecar JSON document into a server list. Accepts the usual
// { "mcpServers": { "<name>": { "command", "args", "env" } } } shape (or a bare
// object of the same). Entries without a command are dropped.
std::vector<McpServerConfig> parse_mcp_servers(const nlohmann::json& doc);

// Read and parse the sidecar file; a missing or invalid file yields no servers.
std::vector<McpServerConfig> load_mcp_servers(const std::string& path);

// Builds a transport for a server. Defaults to stdio; tests inject their own.
using TransportFactory = std::function<TransportPtr(const McpServerConfig&)>;

// Start each server and wrap its tools as McpTools; servers that fail to start
// are logged and skipped. With more than one server, names are namespaced
// `<server>__<tool>`. Each tool shares ownership of its client, keeping it alive.
std::vector<std::unique_ptr<Tool>> make_mcp_tools(
    const std::vector<McpServerConfig>& servers,
    const TransportFactory& factory = {});

}  // namespace llmcli::mcp
