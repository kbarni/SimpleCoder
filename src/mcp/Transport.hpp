#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace llmcli::mcp {

// One configured MCP server, loaded from the sidecar JSON file. Only stdio is
// implemented, so command/args/env describe the subprocess to spawn.
struct McpServerConfig {
  std::string name;
  std::string command;
  std::vector<std::string> args;
  std::vector<std::pair<std::string, std::string>> env;
};

// Result of one JSON-RPC round-trip. `ok` is false only on a transport failure
// (write, EOF, timeout, bad parse); a JSON-RPC error object comes back as a
// normal response for the client to interpret.
struct RpcResult {
  bool ok = false;
  nlohmann::json response;
  std::string error;
};

// A channel to one MCP server, injectable like HttpFn. The client speaks
// JSON-RPC; the transport just frames messages. Never throws.
class Transport {
 public:
  virtual ~Transport() = default;

  // Send a request and return the response with the matching id, skipping any
  // notifications in between.
  virtual RpcResult request(const nlohmann::json& req) = 0;

  // Fire-and-forget notification. False on a write failure.
  virtual bool notify(const nlohmann::json& note) = 0;
};

using TransportPtr = std::unique_ptr<Transport>;

// Spawn the server and speak newline-delimited JSON-RPC over its stdin/stdout;
// the child's stderr goes to /dev/null. nullptr if it can't be started.
TransportPtr open_stdio_transport(const McpServerConfig& cfg);

}  // namespace llmcli::mcp
