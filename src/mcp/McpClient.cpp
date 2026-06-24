#include "mcp/McpClient.hpp"

#include <fstream>
#include <sstream>
#include <utility>

#include "agent/Tool.hpp"
#include "util/Log.hpp"

#ifndef SIMPLECODER_VERSION
#define SIMPLECODER_VERSION "0.0.0"
#endif

namespace llmcli::mcp {

using nlohmann::json;

namespace {

constexpr const char* kProtocolVersion = "2024-11-05";

// Pull a JSON-RPC error message out of a response, or "" if it isn't an error.
std::string rpc_error(const json& resp) {
  auto e = resp.find("error");
  if (e == resp.end() || !e->is_object()) return "";
  std::string msg = e->value("message", "unknown error");
  if (auto c = e->find("code"); c != e->end() && c->is_number())
    msg += " (code " + std::to_string(c->get<long long>()) + ")";
  return msg;
}

}  // namespace

McpClient::McpClient(TransportPtr transport, std::string name)
    : transport_(std::move(transport)), name_(std::move(name)) {}

bool McpClient::start() {
  // 1) initialize handshake.
  json init = {{"jsonrpc", "2.0"},
               {"id", next_id()},
               {"method", "initialize"},
               {"params",
                {{"protocolVersion", kProtocolVersion},
                 {"capabilities", json::object()},
                 {"clientInfo",
                  {{"name", "SimpleCoder"}, {"version", SIMPLECODER_VERSION}}}}}};
  RpcResult r = transport_->request(init);
  if (!r.ok) {
    last_error_ = r.error;
    return false;
  }
  if (std::string err = rpc_error(r.response); !err.empty()) {
    last_error_ = "initialize failed: " + err;
    return false;
  }

  // 2) tell the server we're ready (notification, no reply).
  transport_->notify({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}});

  // 3) tools/list, following pagination cursors.
  tools_.clear();
  std::string cursor;
  for (;;) {
    json req = {{"jsonrpc", "2.0"},
                {"id", next_id()},
                {"method", "tools/list"}};
    if (!cursor.empty()) req["params"] = {{"cursor", cursor}};

    RpcResult lr = transport_->request(req);
    if (!lr.ok) {
      last_error_ = lr.error;
      return false;
    }
    if (std::string err = rpc_error(lr.response); !err.empty()) {
      last_error_ = "tools/list failed: " + err;
      return false;
    }

    const json& result = lr.response["result"];
    if (auto ts = result.find("tools"); ts != result.end() && ts->is_array()) {
      for (const json& t : *ts) {
        McpToolInfo info;
        info.name = t.value("name", "");
        info.description = t.value("description", "");
        if (auto s = t.find("inputSchema"); s != t.end() && s->is_object())
          info.input_schema = *s;
        if (!info.name.empty()) tools_.push_back(std::move(info));
      }
    }

    auto cur = result.find("nextCursor");
    if (cur != result.end() && cur->is_string() && !cur->get<std::string>().empty())
      cursor = cur->get<std::string>();
    else
      break;
  }
  return true;
}

McpCallResult McpClient::call(const std::string& tool, const json& args) {
  json req = {{"jsonrpc", "2.0"},
              {"id", next_id()},
              {"method", "tools/call"},
              {"params",
               {{"name", tool},
                {"arguments", args.is_null() ? json::object() : args}}}};

  RpcResult r = transport_->request(req);
  if (!r.ok) return {false, "error: " + r.error};
  if (std::string err = rpc_error(r.response); !err.empty())
    return {false, "error: " + err};

  const auto res = r.response.find("result");
  if (res == r.response.end() || !res->is_object())
    return {false, "error: malformed tools/call response"};

  // Join the text content blocks; note any non-text blocks so the model knows.
  std::string text;
  if (auto content = res->find("content");
      content != res->end() && content->is_array()) {
    for (const json& block : *content) {
      const std::string type = block.value("type", "");
      if (type == "text") {
        text += block.value("text", "");
      } else if (!type.empty()) {
        text += "[" + type + " content]";
      }
    }
  }

  const bool is_error = res->value("isError", false);
  return {!is_error, text};
}

std::vector<McpServerConfig> parse_mcp_servers(const json& doc) {
  std::vector<McpServerConfig> out;
  if (!doc.is_object()) return out;

  // Accept { "mcpServers": {...} } or a bare object of servers.
  const json* servers = &doc;
  if (auto it = doc.find("mcpServers"); it != doc.end() && it->is_object())
    servers = &*it;

  for (auto it = servers->begin(); it != servers->end(); ++it) {
    if (it.key() == "mcpServers" || !it.value().is_object()) continue;
    const json& s = it.value();
    McpServerConfig cfg;
    cfg.name = it.key();
    cfg.command = s.value("command", "");
    if (cfg.command.empty()) continue;  // stdio servers need a command
    if (auto a = s.find("args"); a != s.end() && a->is_array()) {
      for (const json& arg : *a)
        if (arg.is_string()) cfg.args.push_back(arg.get<std::string>());
    }
    if (auto e = s.find("env"); e != s.end() && e->is_object()) {
      for (auto ev = e->begin(); ev != e->end(); ++ev)
        if (ev.value().is_string())
          cfg.env.emplace_back(ev.key(), ev.value().get<std::string>());
    }
    out.push_back(std::move(cfg));
  }
  return out;
}

std::vector<McpServerConfig> load_mcp_servers(const std::string& path) {
  if (path.empty()) return {};
  std::ifstream f(path);
  if (!f) {
    log().warn("mcp: could not open server file: " + path);
    return {};
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  json doc = json::parse(ss.str(), nullptr, /*allow_exceptions=*/false);
  if (doc.is_discarded()) {
    log().warn("mcp: invalid JSON in server file: " + path);
    return {};
  }
  return parse_mcp_servers(doc);
}

std::vector<std::unique_ptr<Tool>> make_mcp_tools(
    const std::vector<McpServerConfig>& servers, const TransportFactory& factory) {
  std::vector<std::unique_ptr<Tool>> tools;
  const bool namespaced = servers.size() > 1;

  for (const auto& cfg : servers) {
    TransportPtr transport =
        factory ? factory(cfg) : open_stdio_transport(cfg);
    if (!transport) {
      log().warn("mcp: failed to start server '" + cfg.name + "'");
      continue;
    }

    auto client = std::make_shared<McpClient>(std::move(transport), cfg.name);
    if (!client->start()) {
      log().warn("mcp: server '" + cfg.name +
                 "' handshake failed: " + client->last_error());
      continue;
    }

    for (const McpToolInfo& info : client->tools()) {
      std::string exposed = namespaced ? cfg.name + "__" + info.name : info.name;
      tools.push_back(std::make_unique<McpTool>(
          client, exposed, info.name, info.description, info.input_schema));
    }
    log().info("mcp: server '" + cfg.name + "' exposed " +
               std::to_string(client->tools().size()) + " tool(s)");
  }
  return tools;
}

}  // namespace llmcli::mcp
