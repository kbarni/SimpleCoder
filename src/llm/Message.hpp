#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace llmcli {

// A participant role in the conversation, matching the OpenAI chat roles.
enum class Role { System, User, Assistant, Tool };

inline std::string_view to_string(Role role) {
  switch (role) {
    case Role::System:
      return "system";
    case Role::User:
      return "user";
    case Role::Assistant:
      return "assistant";
    case Role::Tool:
      return "tool";
  }
  return "user";  // unreachable; keeps the compiler happy
}

inline std::optional<Role> role_from_string(std::string_view s) {
  if (s == "system") return Role::System;
  if (s == "user") return Role::User;
  if (s == "assistant") return Role::Assistant;
  if (s == "tool") return Role::Tool;
  return std::nullopt;
}

// A single tool invocation requested by the assistant. `arguments` holds the
// raw JSON object string exactly as the model produced it; parsing happens at
// the point of execution (T10).
struct ToolCall {
  std::string id;
  std::string name;
  std::string arguments;  // raw JSON string, e.g. {"path":"foo.txt"}

  friend bool operator==(const ToolCall&, const ToolCall&) = default;
};

// An image attached to a user turn, sent as an OpenAI image_url content part.
// data_url is the ready-to-send "data:<type>;base64,…"; name/bytes feed the TUI
// placeholder (it can't show pixels). A parallel vector, so text stays unchanged.
struct ImagePart {
  std::string name;        // display name, e.g. "photo.png"
  std::string media_type;  // e.g. "image/png"
  std::string data_url;    // "data:image/png;base64,…" — ready to serialize
  std::size_t bytes = 0;   // original file size, for the placeholder

  friend bool operator==(const ImagePart&, const ImagePart&) = default;
};

// One message in the conversation history.
//   - Assistant messages may carry `tool_calls`.
//   - Tool-result messages (role == Tool) set `tool_call_id` to the id of the
//     call they answer, and put the result in `content`.
//   - User messages may carry `images` (serialized as image_url content parts).
struct Message {
  Role role = Role::User;
  std::string content;
  std::vector<ToolCall> tool_calls;  // set on assistant tool-call messages
  std::string tool_call_id;          // set on tool-result messages
  std::vector<ImagePart> images;     // set on user messages with attachments

  friend bool operator==(const Message&, const Message&) = default;

  static Message system(std::string text) {
    return {Role::System, std::move(text), {}, {}, {}};
  }
  static Message user(std::string text) {
    return {Role::User, std::move(text), {}, {}, {}};
  }
  static Message user(std::string text, std::vector<ImagePart> images) {
    return {Role::User, std::move(text), {}, {}, std::move(images)};
  }
  static Message assistant(std::string text) {
    return {Role::Assistant, std::move(text), {}, {}, {}};
  }
  // A tool result answering the call with the given id.
  static Message tool_result(std::string call_id, std::string text) {
    return {Role::Tool, std::move(text), {}, std::move(call_id), {}};
  }
};

}  // namespace llmcli
