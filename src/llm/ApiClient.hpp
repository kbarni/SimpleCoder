#pragma once

#include <atomic>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "app/Config.hpp"
#include "llm/ChatResponse.hpp"
#include "llm/Message.hpp"

namespace llmcli {

// Outcome of a chat request.
struct ApiResult {
  bool ok = false;            // true iff a 2xx response was fully received
  long http_status = 0;       // HTTP status, or 0 if the connection failed
  std::string error;          // human-readable message when !ok
  std::string content;        // assembled assistant text
  std::string reasoning;      // assembled reasoning_content, if any
  std::vector<ToolCall> tool_calls;
  std::string finish_reason;
  int prompt_tokens = 0;      // 0 unless the server reported usage
  int completion_tokens = 0;
  int total_tokens = 0;       // prompt + completion; approximates context used
  double gen_seconds = 0;     // wall time from the first streamed token to the
                              // end of the stream; 0 if nothing streamed
  bool canceled = false;      // true iff aborted via the cancel flag (not ok)
};

// Sends streaming chat-completion requests to an OpenAI-compatible endpoint via
// libcurl. One client wraps one Config; it is safe to reuse across calls but
// not concurrently from multiple threads.
class ApiClient {
 public:
  explicit ApiClient(Config cfg);

  // POST {base_url}/chat/completions with stream:true. `on_content` and
  // `on_reasoning` (both optional) are invoked with each content / reasoning
  // delta as it arrives. If `cancel` is non-null and becomes true mid-transfer,
  // the request is aborted and the result is marked `canceled`. Never throws;
  // failures are reported in the result.
  ApiResult chat(const std::vector<Message>& messages,
                 const nlohmann::json& tools = nlohmann::json::array(),
                 const StreamParser::ContentCallback& on_content = {},
                 const StreamParser::ContentCallback& on_reasoning = {},
                 const std::atomic<bool>* cancel = nullptr);

  // Best-effort query for the server's context window size, in tokens. Probes
  // llama.cpp's GET /props (default_generation_settings.n_ctx) then vLLM's
  // GET /v1/models (data[].max_model_len). Returns nullopt if neither answers
  // (e.g. server unreachable or an endpoint not implemented).
  std::optional<int> probe_context_size();

  // The model ids the server advertises (GET /v1/models), for `/model`. Empty if
  // the server is unreachable or lists none.
  std::vector<std::string> list_models();

 private:
  Config cfg_;
};

// Extract model ids from an OpenAI /v1/models payload ({"data":[{"id":…}]}).
// Tolerant of missing fields; entries without a string id are skipped.
std::vector<std::string> parse_model_ids(const nlohmann::json& j);

}  // namespace llmcli
