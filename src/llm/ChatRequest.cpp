#include "llm/ChatRequest.hpp"

namespace llmcli {

using nlohmann::json;

json to_content_parts(const Message& msg) {
  json parts = json::array();
  if (!msg.content.empty()) {
    parts.push_back({{"type", "text"}, {"text", msg.content}});
  }
  for (const ImagePart& img : msg.images) {
    parts.push_back({{"type", "image_url"},
                     {"image_url", {{"url", img.data_url}}}});
  }
  return parts;
}

json to_json(const Message& msg) {
  json j;
  j["role"] = std::string(to_string(msg.role));

  // A user turn with image attachments uses the structured parts array; without
  // images it keeps the plain-string form so non-vision servers are unaffected.
  if (msg.role == Role::User && !msg.images.empty()) {
    j["content"] = to_content_parts(msg);
    return j;
  }

  switch (msg.role) {
    case Role::Assistant:
      // An assistant turn either carries text or requests tool calls. OpenAI
      // expects content to be present (possibly null) alongside tool_calls.
      if (msg.tool_calls.empty()) {
        j["content"] = msg.content;
      } else {
        j["content"] = msg.content.empty() ? json(nullptr) : json(msg.content);
        json calls = json::array();
        for (const ToolCall& tc : msg.tool_calls) {
          calls.push_back({{"id", tc.id},
                           {"type", "function"},
                           {"function",
                            {{"name", tc.name},
                             // arguments is a JSON-encoded *string* per the API
                             {"arguments", tc.arguments}}}});
        }
        j["tool_calls"] = std::move(calls);
      }
      break;

    case Role::Tool:
      j["content"] = msg.content;
      j["tool_call_id"] = msg.tool_call_id;
      break;

    case Role::System:
    case Role::User:
      j["content"] = msg.content;
      break;
  }
  return j;
}

json build_chat_request(const Config& cfg, const std::vector<Message>& messages,
                        const json& tools, bool stream) {
  json body;
  body["model"] = cfg.model;
  body["stream"] = stream;
  body["temperature"] = cfg.temperature;

  // Ask streaming responses to include a trailing token-usage chunk so the UI
  // can report context usage. Harmless for servers that ignore it.
  if (stream) {
    body["stream_options"] = {{"include_usage", true}};
  }

  json msgs = json::array();
  for (const Message& m : messages) {
    msgs.push_back(to_json(m));
  }
  body["messages"] = std::move(msgs);

  if (tools.is_array() && !tools.empty()) {
    body["tools"] = tools;
  }
  return body;
}

}  // namespace llmcli
