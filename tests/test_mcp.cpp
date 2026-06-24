#include "mcp/McpClient.hpp"

#include <memory>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "agent/Tool.hpp"
#include "mcp/Transport.hpp"

using namespace llmcli;
using nlohmann::json;

namespace {

// A scripted Transport: answers the JSON-RPC handshake and serves canned
// results, so McpClient/McpTool are exercised with no subprocess (mirrors the
// StubHttp seam in the web-tools tests).
struct ScriptedTransport : mcp::Transport {
  json tools_list = {{"tools", json::array()}};  // result of tools/list
  json call_result = {{"content", json::array()}};  // result of tools/call
  bool fail_initialize = false;
  bool fail_call = false;        // make tools/call return a JSON-RPC error
  bool transport_fails = false;  // simulate a write/EOF/timeout failure

  // Captured for assertions.
  std::string last_call_name;
  json last_call_args;
  bool initialized_notified = false;

  mcp::RpcResult request(const json& req) override {
    if (transport_fails) return {false, {}, "connection reset"};
    const std::string method = req.value("method", "");
    json resp = {{"jsonrpc", "2.0"}, {"id", req.at("id")}};
    if (method == "initialize") {
      if (fail_initialize)
        resp["error"] = {{"code", -32000}, {"message", "refused"}};
      else
        resp["result"] = {{"protocolVersion", "2024-11-05"}};
    } else if (method == "tools/list") {
      resp["result"] = tools_list;
    } else if (method == "tools/call") {
      last_call_name = req["params"].value("name", "");
      last_call_args = req["params"].value("arguments", json::object());
      if (fail_call)
        resp["error"] = {{"code", -32000}, {"message", "tool blew up"}};
      else
        resp["result"] = call_result;
    } else {
      resp["error"] = {{"code", -32601}, {"message", "method not found"}};
    }
    return {true, resp, ""};
  }

  bool notify(const json& note) override {
    if (note.value("method", "") == "notifications/initialized")
      initialized_notified = true;
    return true;
  }
};

// A tools/list result with a single "echo" tool.
json one_tool_list() {
  json schema = {{"type", "object"},
                 {"properties", {{"text", {{"type", "string"}}}}},
                 {"required", json::array({"text"})}};
  json tool = {{"name", "echo"},
               {"description", "Echo text back"},
               {"inputSchema", schema}};
  return {{"tools", json::array({tool})}};
}

}  // namespace

// --- handshake + tools/list ------------------------------------------------

TEST_CASE("McpClient runs the handshake and lists tools", "[mcp]") {
  auto t = std::make_unique<ScriptedTransport>();
  t->tools_list = one_tool_list();
  ScriptedTransport* raw = t.get();

  mcp::McpClient client(std::move(t), "srv");
  REQUIRE(client.start());
  CHECK(raw->initialized_notified);  // sent notifications/initialized
  REQUIRE(client.tools().size() == 1);
  CHECK(client.tools()[0].name == "echo");
  CHECK(client.tools()[0].input_schema["type"] == "object");
}

TEST_CASE("McpClient.start fails when initialize errors", "[mcp]") {
  auto t = std::make_unique<ScriptedTransport>();
  t->fail_initialize = true;
  mcp::McpClient client(std::move(t));
  CHECK_FALSE(client.start());
  CHECK(client.last_error().find("refused") != std::string::npos);
}

TEST_CASE("McpClient.start fails on a transport error", "[mcp]") {
  auto t = std::make_unique<ScriptedTransport>();
  t->transport_fails = true;
  mcp::McpClient client(std::move(t));
  CHECK_FALSE(client.start());
  CHECK(client.last_error().find("connection reset") != std::string::npos);
}

// --- McpTool: schema + tools/call round-trip --------------------------------

TEST_CASE("McpTool exposes the server's schema", "[mcp]") {
  auto t = std::make_unique<ScriptedTransport>();
  t->tools_list = one_tool_list();
  auto client = std::make_shared<mcp::McpClient>(std::move(t));
  REQUIRE(client->start());

  const auto& info = client->tools()[0];
  McpTool tool(client, info.name, info.name, info.description, info.input_schema);

  CHECK(tool.name() == "echo");
  CHECK(tool.requires_confirmation());  // remote tools always gate
  json s = tool.schema();
  CHECK(s["type"] == "function");
  CHECK(s["function"]["name"] == "echo");
  CHECK(s["function"]["description"] == "Echo text back");
  // The inputSchema rides straight through as "parameters".
  CHECK(s["function"]["parameters"]["properties"]["text"]["type"] == "string");
}

TEST_CASE("McpTool.execute round-trips arguments and returns content", "[mcp]") {
  auto t = std::make_unique<ScriptedTransport>();
  t->tools_list = one_tool_list();
  t->call_result = {{"content", json::array({{{"type", "text"},
                                              {"text", "hello world"}}})}};
  ScriptedTransport* raw = t.get();
  auto client = std::make_shared<mcp::McpClient>(std::move(t));
  REQUIRE(client->start());

  McpTool tool(client, "echo", "echo", "", json::object());
  ToolResult res = tool.execute({{"text", "hi"}});

  REQUIRE(res.ok);
  CHECK(res.output == "hello world");
  CHECK(raw->last_call_name == "echo");
  CHECK(raw->last_call_args["text"] == "hi");
}

TEST_CASE("McpTool surfaces an isError result as a failed ToolResult", "[mcp]") {
  auto t = std::make_unique<ScriptedTransport>();
  t->tools_list = one_tool_list();
  t->call_result = {{"isError", true},
                    {"content", json::array({{{"type", "text"},
                                              {"text", "boom"}}})}};
  auto client = std::make_shared<mcp::McpClient>(std::move(t));
  REQUIRE(client->start());

  McpTool tool(client, "echo", "echo", "", json::object());
  ToolResult res = tool.execute({{"text", "x"}});
  CHECK_FALSE(res.ok);
  CHECK(res.output == "boom");
}

TEST_CASE("McpTool surfaces a JSON-RPC error as a failed ToolResult", "[mcp]") {
  auto t = std::make_unique<ScriptedTransport>();
  t->tools_list = one_tool_list();
  t->fail_call = true;  // server answers tools/call with an error object
  auto client = std::make_shared<mcp::McpClient>(std::move(t));
  REQUIRE(client->start());

  McpTool tool(client, "echo", "echo", "", json::object());
  ToolResult res = tool.execute({{"text", "x"}});
  CHECK_FALSE(res.ok);
  CHECK(res.output.find("tool blew up") != std::string::npos);
}

TEST_CASE("McpTool returns an empty result for empty content", "[mcp]") {
  auto t = std::make_unique<ScriptedTransport>();
  t->tools_list = one_tool_list();  // default call_result has empty content
  auto client = std::make_shared<mcp::McpClient>(std::move(t));
  REQUIRE(client->start());

  McpTool tool(client, "echo", "echo", "", json::object());
  ToolResult res = tool.execute({{"text", "x"}});
  CHECK(res.ok);
  CHECK(res.output.empty());
}

// --- parse_mcp_servers ------------------------------------------------------

TEST_CASE("parse_mcp_servers reads the mcpServers shape", "[mcp][config]") {
  json doc = {{"mcpServers",
               {{"fs",
                 {{"command", "npx"},
                  {"args", json::array({"-y", "server-fs", "/data"})},
                  {"env", {{"TOKEN", "abc"}}}}},
                {"git", {{"command", "uvx"}, {"args", json::array({"mcp-git"})}}}}}};
  auto servers = mcp::parse_mcp_servers(doc);
  REQUIRE(servers.size() == 2);

  // Find by name (object iteration order is preserved by nlohmann for objects
  // built this way, but assert by lookup to be safe).
  const mcp::McpServerConfig* fs = nullptr;
  for (auto& s : servers)
    if (s.name == "fs") fs = &s;
  REQUIRE(fs);
  CHECK(fs->command == "npx");
  REQUIRE(fs->args.size() == 3);
  CHECK(fs->args[1] == "server-fs");
  REQUIRE(fs->env.size() == 1);
  CHECK(fs->env[0].first == "TOKEN");
  CHECK(fs->env[0].second == "abc");
}

TEST_CASE("parse_mcp_servers drops entries without a command", "[mcp][config]") {
  json doc = {{"mcpServers", {{"bad", {{"args", json::array({"x"})}}}}}};
  CHECK(mcp::parse_mcp_servers(doc).empty());
}

TEST_CASE("parse_mcp_servers tolerates garbage", "[mcp][config]") {
  CHECK(mcp::parse_mcp_servers(json::array()).empty());
  CHECK(mcp::parse_mcp_servers(json(42)).empty());
}

// --- make_mcp_tools (transport factory seam) --------------------------------

TEST_CASE("make_mcp_tools wraps every server tool, namespacing on collisions",
          "[mcp]") {
  std::vector<mcp::McpServerConfig> servers = {{"a", "cmd", {}, {}},
                                               {"b", "cmd", {}, {}}};

  mcp::TransportFactory factory = [](const mcp::McpServerConfig&) {
    auto t = std::make_unique<ScriptedTransport>();
    t->tools_list = one_tool_list();
    return mcp::TransportPtr(std::move(t));
  };

  auto tools = mcp::make_mcp_tools(servers, factory);
  REQUIRE(tools.size() == 2);
  // Two servers -> names are namespaced <server>__<tool> to avoid collision.
  bool has_a = false, has_b = false;
  for (auto& t : tools) {
    if (t->name() == "a__echo") has_a = true;
    if (t->name() == "b__echo") has_b = true;
    CHECK(t->requires_confirmation());
  }
  CHECK(has_a);
  CHECK(has_b);
}

TEST_CASE("make_mcp_tools skips a server that fails to start", "[mcp]") {
  std::vector<mcp::McpServerConfig> servers = {{"ok", "cmd", {}, {}},
                                               {"bad", "cmd", {}, {}}};

  mcp::TransportFactory factory = [](const mcp::McpServerConfig& cfg) {
    auto t = std::make_unique<ScriptedTransport>();
    t->tools_list = one_tool_list();
    if (cfg.name == "bad") t->fail_initialize = true;
    return mcp::TransportPtr(std::move(t));
  };

  auto tools = mcp::make_mcp_tools(servers, factory);
  REQUIRE(tools.size() == 1);
  CHECK(tools[0]->name() == "ok__echo");
}

TEST_CASE("make_mcp_tools skips a server whose transport can't open", "[mcp]") {
  std::vector<mcp::McpServerConfig> servers = {{"x", "cmd", {}, {}}};
  mcp::TransportFactory factory = [](const mcp::McpServerConfig&) {
    return mcp::TransportPtr(nullptr);  // couldn't spawn
  };
  CHECK(mcp::make_mcp_tools(servers, factory).empty());
}
