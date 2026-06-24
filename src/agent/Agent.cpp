#include "agent/Agent.hpp"

#include "mcp/McpClient.hpp"

namespace llmcli {

using nlohmann::json;

namespace {

Tool* find_tool(const std::vector<std::unique_ptr<Tool>>& tools,
                const std::string& name) {
  for (const auto& t : tools) {
    if (t->name() == name) return t.get();
  }
  return nullptr;
}

}  // namespace

ApiResult run_tool_loop(std::vector<Message>& history, const ChatRound& round,
                        const std::vector<std::unique_ptr<Tool>>& tools,
                        ConfirmGate& gate, const AgentCallbacks& cb,
                        int max_iters, const std::atomic<bool>* cancel) {
  for (int iter = 0; iter < max_iters; ++iter) {
    if (cancel && cancel->load()) {
      ApiResult r;
      r.canceled = true;
      r.error = "request canceled";
      return r;
    }
    ApiResult res = round(history);
    if (!res.ok) return res;

    if (res.tool_calls.empty()) {
      history.push_back(Message::assistant(res.content));
      return res;
    }

    // Record the assistant turn that requested the tools.
    history.push_back(
        Message{Role::Assistant, res.content, res.tool_calls, "", {}});

    for (const ToolCall& call : res.tool_calls) {
      if (cb.on_tool_call) cb.on_tool_call(call);

      ToolResult tr;
      Tool* tool = find_tool(tools, call.name);
      if (!tool) {
        tr = {false, "error: unknown tool '" + call.name + "'"};
      } else {
        json args = json::parse(call.arguments, nullptr,
                                /*allow_exceptions=*/false);
        if (args.is_discarded()) args = json::object();

        if (!gate.allow(*tool, tool->confirm_details(args))) {
          tr = {false, "User declined to run this tool."};
        } else {
          tr = tool->execute(args);
        }
      }

      if (cb.on_tool_result) cb.on_tool_result(call, tr);
      history.push_back(Message::tool_result(call.id, tr.output));
    }
    // Loop: re-request now that the tool results are in the history.
  }

  ApiResult failed;
  failed.error = "tool loop exceeded " + std::to_string(max_iters) +
                 " iterations";
  return failed;
}

Agent::Agent(Config cfg, Confirmer confirmer)
    : cfg_(std::move(cfg)),
      client_(cfg_),
      tools_(default_tools(WebToolsConfig{cfg_.allow_web, cfg_.search_url,
                                          cfg_.search_api_key,
                                          cfg_.search_backend})),
      tool_schemas_(json::array()),
      gate_(std::move(confirmer),
            makeConfirmPolicy(cfg_.allow_rules, cfg_.deny_rules)) {
  // Append tools from any configured MCP servers (T28). Each server is spawned
  // and handshaked here; failures are logged and skipped (see make_mcp_tools).
  if (!cfg_.mcp_config.empty()) {
    auto servers = mcp::load_mcp_servers(cfg_.mcp_config);
    for (auto& t : mcp::make_mcp_tools(servers))
      tools_.push_back(std::move(t));
  }

  for (const auto& t : tools_) tool_schemas_.push_back(t->schema());

  // Prefer an explicit config value; otherwise probe the server once at startup.
  context_size_ = cfg_.context_size;
  if (context_size_ <= 0) {
    if (auto n = client_.probe_context_size()) context_size_ = *n;
  }

  reset();
}

void Agent::reset() {
  history_.clear();
  if (!cfg_.system_prompt.empty()) {
    history_.push_back(Message::system(cfg_.system_prompt));
  }
}

void Agent::compact_into_summary(const std::string& summary) {
  history_.clear();
  if (!cfg_.system_prompt.empty()) {
    history_.push_back(Message::system(cfg_.system_prompt));
  }
  history_.push_back(Message::system(
      "Summary of the conversation so far (earlier turns were compacted into "
      "this):\n" + summary));
  last_total_tokens_ = 0;  // recomputed on the next real turn
}

ApiResult Agent::send(const std::string& user_message, const AgentCallbacks& cb,
                      const std::atomic<bool>* cancel) {
  return send(user_message, {}, cb, cancel);
}

ApiResult Agent::send(const std::string& user_message,
                      std::vector<ImagePart> images, const AgentCallbacks& cb,
                      const std::atomic<bool>* cancel) {
  const std::size_t mark = history_.size();  // restore point on failure
  history_.push_back(Message::user(user_message, std::move(images)));

  ChatRound round = [&](const std::vector<Message>& h) {
    return client_.chat(h, tool_schemas_, cb.on_content, cb.on_reasoning,
                        cancel);
  };

  ApiResult res =
      run_tool_loop(history_, round, tools_, gate_, cb,
                    cfg_.max_tool_iterations, cancel);

  if (!res.ok) {
    history_.resize(mark);  // drop the user turn and any partial tool messages
  } else {
    if (res.total_tokens > 0)
      last_total_tokens_ = res.total_tokens;  // last turn's prompt + completion
    if (res.gen_seconds > 0 && res.completion_tokens > 0)
      last_tokens_per_second_ = res.completion_tokens / res.gen_seconds;
  }
  return res;
}

}  // namespace llmcli
