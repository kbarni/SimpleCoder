#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "agent/Confirm.hpp"
#include "agent/Tool.hpp"
#include "app/Config.hpp"
#include "llm/ApiClient.hpp"
#include "llm/ChatResponse.hpp"
#include "llm/Message.hpp"

namespace llmcli {

// Callbacks for observing a turn as it progresses.
struct AgentCallbacks {
  StreamParser::ContentCallback on_content;     // assistant text delta
  StreamParser::ContentCallback on_reasoning;   // reasoning delta
  std::function<void(const ToolCall&)> on_tool_call;            // about to run
  std::function<void(const ToolCall&, const ToolResult&)> on_tool_result;
};

// Performs one chat round against the given history, returning the result.
using ChatRound = std::function<ApiResult(const std::vector<Message>&)>;

// Drive the tool-calling loop: request a completion; while the assistant
// returns tool calls, run each (gated by `gate`), append the results, and
// re-request — until a plain message is returned or `max_iters` is hit. Mutates
// `history` in place. Extracted from Agent so it can be tested with a mock
// ChatRound, independent of the network. If `cancel` is non-null and set, the
// loop stops before the next request and returns a canceled result.
ApiResult run_tool_loop(std::vector<Message>& history, const ChatRound& round,
                        const std::vector<std::unique_ptr<Tool>>& tools,
                        ConfirmGate& gate, const AgentCallbacks& cb,
                        int max_iters = 8,
                        const std::atomic<bool>* cancel = nullptr);

// Owns the conversation, the tool set, and the confirmation gate, and drives
// turns against the API client.
class Agent {
 public:
  // `confirmer` decides on gated tool calls (write_file, run_bash). If empty,
  // those tools fail closed.
  explicit Agent(Config cfg, Confirmer confirmer = {});

  // Append `user_message`, run the tool loop with the running history as
  // context, and return the final result. If `cancel` is non-null and set
  // mid-flight, the turn aborts and history is rolled back. Never throws.
  ApiResult send(const std::string& user_message, const AgentCallbacks& cb = {},
                 const std::atomic<bool>* cancel = nullptr);

  // As above, attaching `images` to the user turn (T29). They are serialized as
  // OpenAI image_url content parts and re-sent on every turn until /compact.
  ApiResult send(const std::string& user_message, std::vector<ImagePart> images,
                 const AgentCallbacks& cb = {},
                 const std::atomic<bool>* cancel = nullptr);

  const std::vector<Message>& history() const { return history_; }
  const Config& config() const { return cfg_; }

  // total_tokens reported by the most recent successful turn (0 if none yet or
  // the server reported no usage); approximates the context currently in use.
  int last_total_tokens() const { return last_total_tokens_; }

  // The server's context window in tokens (from config or auto-detection), or 0
  // if unknown. Used with last_total_tokens() for the status-bar usage gauge.
  int context_size() const { return context_size_; }

  // Generation speed of the most recent turn in tokens/second (completion tokens
  // over the streamed generation time), or 0 if not measurable yet.
  double last_tokens_per_second() const { return last_tokens_per_second_; }

  // Switch the model used for subsequent requests (e.g. via /model).
  void set_model(std::string model) { cfg_.model = std::move(model); }

  // Model ids the server advertises, for `/model`. Empty if none/unreachable.
  std::vector<std::string> list_models() { return client_.list_models(); }

  // Set the system prompt seeded into a fresh conversation (e.g. after /init
  // writes an AGENTS.md). Takes effect on the next reset()/new conversation.
  void set_system_prompt(std::string prompt) {
    cfg_.system_prompt = std::move(prompt);
  }

  void reset();

  // Replace the conversation history with `summary`, preserving the system
  // prompt. Used by /compact to shrink the context the model carries forward:
  // afterwards history is just the system prompt (if any) plus a system note
  // holding the summary. Resets the token gauge until the next turn.
  void compact_into_summary(const std::string& summary);

 private:
  Config cfg_;
  ApiClient client_;
  std::vector<std::unique_ptr<Tool>> tools_;
  nlohmann::json tool_schemas_;
  ConfirmGate gate_;
  std::vector<Message> history_;
  int last_total_tokens_ = 0;
  int context_size_ = 0;
  double last_tokens_per_second_ = 0;
};

}  // namespace llmcli
