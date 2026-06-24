#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "llm/Message.hpp"

namespace llmcli {

// Incrementally parses a streamed chat-completions response (Server-Sent
// Events). Bytes may be fed in arbitrary chunks, matching libcurl's write
// callback; the parser buffers partial lines internally.
//
// Each SSE event is a `data: {json}` line; the stream ends with `data: [DONE]`.
// Content deltas are concatenated (and reported live via the callback), while
// tool-call fragments — which arrive split across events and keyed by index —
// are reassembled into complete ToolCalls.
class StreamParser {
 public:
  // Invoked with each incremental content delta, for live UI rendering.
  using ContentCallback = std::function<void(std::string_view)>;

  explicit StreamParser(ContentCallback on_content = {},
                        ContentCallback on_reasoning = {});

  // Feed raw response bytes. Safe to call repeatedly with any byte boundaries.
  void feed(std::string_view bytes);

  // True once `data: [DONE]` has been seen.
  bool done() const { return done_; }

  // The full assistant text accumulated so far.
  const std::string& content() const { return content_; }

  // Accumulated reasoning / "thinking" text (delta.reasoning_content), for
  // models/servers that expose a separate chain-of-thought channel.
  const std::string& reasoning() const { return reasoning_; }

  // The reported finish reason (e.g. "stop", "tool_calls"), if any.
  const std::string& finish_reason() const { return finish_reason_; }

  // Token usage from the trailing `usage` chunk, if the server reported it
  // (requires stream_options.include_usage; 0 when absent). total_tokens is the
  // prompt + completion count and approximates the context now in use.
  int prompt_tokens() const { return prompt_tokens_; }
  int completion_tokens() const { return completion_tokens_; }
  int total_tokens() const { return total_tokens_; }

  // Tool calls reassembled from the stream, in ascending index order.
  std::vector<ToolCall> tool_calls() const;

 private:
  void process_line(std::string_view line);
  void process_chunk(std::string_view json_payload);

  ContentCallback on_content_;
  ContentCallback on_reasoning_;
  std::string buffer_;         // bytes not yet forming a complete line
  std::string content_;        // accumulated assistant text
  std::string reasoning_;      // accumulated reasoning_content
  std::string finish_reason_;  // last non-null finish_reason
  int prompt_tokens_ = 0;      // from the trailing usage chunk
  int completion_tokens_ = 0;
  int total_tokens_ = 0;
  bool done_ = false;
  std::map<int, ToolCall> tool_by_index_;  // index -> partial tool call
};

}  // namespace llmcli
