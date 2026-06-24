#pragma once

#include <vector>

#include <nlohmann/json.hpp>

#include "app/Config.hpp"
#include "llm/Message.hpp"

namespace llmcli {

// Serialize a single message into its OpenAI chat-completions JSON shape.
nlohmann::json to_json(const Message& msg);

// The structured `content` array for a user message with images: a text part
// (omitted if empty) then one image_url part per attachment. Only used when
// `images` is non-empty; text-only turns keep the plain-string form.
nlohmann::json to_content_parts(const Message& msg);

// Build the JSON body for POST {base_url}/chat/completions.
//
// `model`, `temperature`, and the streaming flag come from `cfg`; `messages` is
// serialized verbatim (the caller is responsible for any leading system
// message). `tools` is an array of tool schema objects (see T10); when empty,
// the "tools" key is omitted.
nlohmann::json build_chat_request(const Config& cfg,
                                  const std::vector<Message>& messages,
                                  const nlohmann::json& tools = nlohmann::json::array(),
                                  bool stream = true);

}  // namespace llmcli
