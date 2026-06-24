#include "llm/ChatRequest.hpp"

#include <catch2/catch_test_macros.hpp>

#include "app/Config.hpp"
#include "llm/Message.hpp"

using llmcli::build_chat_request;
using llmcli::Config;
using llmcli::Message;
using llmcli::to_json;
using llmcli::ToolCall;
using nlohmann::json;

namespace {
Config testCfg() {
  Config c;
  c.base_url = "http://localhost:8080/v1";
  c.model = "test-model";
  c.temperature = 0.3;
  return c;
}
}  // namespace

TEST_CASE("top-level request fields come from config", "[chat_request]") {
  auto body = build_chat_request(testCfg(), {Message::user("hello")});

  CHECK(body["model"] == "test-model");
  CHECK(body["stream"] == true);
  CHECK(body["temperature"] == 0.3);
  REQUIRE(body["messages"].is_array());
  REQUIRE(body["messages"].size() == 1);
  CHECK(body["messages"][0]["role"] == "user");
  CHECK(body["messages"][0]["content"] == "hello");
}

TEST_CASE("stream flag is configurable", "[chat_request]") {
  auto body = build_chat_request(testCfg(), {Message::user("hi")},
                                 json::array(), /*stream=*/false);
  CHECK(body["stream"] == false);
}

TEST_CASE("tools key omitted when empty, present when provided",
          "[chat_request]") {
  auto no_tools = build_chat_request(testCfg(), {Message::user("hi")});
  CHECK_FALSE(no_tools.contains("tools"));

  json tools = json::array();
  tools.push_back({{"type", "function"},
                   {"function", {{"name", "read_file"}}}});
  auto with_tools = build_chat_request(testCfg(), {Message::user("hi")}, tools);
  REQUIRE(with_tools.contains("tools"));
  CHECK(with_tools["tools"][0]["function"]["name"] == "read_file");
}

TEST_CASE("assistant tool-call message serializes in OpenAI shape",
          "[chat_request]") {
  auto assistant = Message::assistant("");
  assistant.tool_calls.push_back(
      ToolCall{"call_1", "read_file", R"({"path":"a.txt"})"});

  json j = to_json(assistant);

  CHECK(j["role"] == "assistant");
  CHECK(j["content"].is_null());  // no text alongside the call
  REQUIRE(j["tool_calls"].is_array());
  REQUIRE(j["tool_calls"].size() == 1);
  const auto& call = j["tool_calls"][0];
  CHECK(call["id"] == "call_1");
  CHECK(call["type"] == "function");
  CHECK(call["function"]["name"] == "read_file");
  // arguments must be a JSON-encoded string, not a nested object
  REQUIRE(call["function"]["arguments"].is_string());
  CHECK(call["function"]["arguments"] == R"({"path":"a.txt"})");
}

TEST_CASE("tool-result message serializes with tool_call_id",
          "[chat_request]") {
  json j = to_json(Message::tool_result("call_1", "file contents"));

  CHECK(j["role"] == "tool");
  CHECK(j["tool_call_id"] == "call_1");
  CHECK(j["content"] == "file contents");
}

TEST_CASE("plain assistant message keeps string content", "[chat_request]") {
  json j = to_json(Message::assistant("the answer is 42"));
  CHECK(j["role"] == "assistant");
  CHECK(j["content"] == "the answer is 42");
  CHECK_FALSE(j.contains("tool_calls"));
}

// --- image content parts (T29) ---------------------------------------------

TEST_CASE("a text-only user message keeps the string content form",
          "[chat_request][image]") {
  json j = to_json(Message::user("just text"));
  CHECK(j["content"].is_string());
  CHECK(j["content"] == "just text");
}

TEST_CASE("a user message with an image serializes as a parts array",
          "[chat_request][image]") {
  llmcli::ImagePart img{"p.png", "image/png", "data:image/png;base64,AAAA", 3};
  json j = to_json(Message::user("what is this?", {img}));

  CHECK(j["role"] == "user");
  REQUIRE(j["content"].is_array());
  REQUIRE(j["content"].size() == 2);
  CHECK(j["content"][0]["type"] == "text");
  CHECK(j["content"][0]["text"] == "what is this?");
  CHECK(j["content"][1]["type"] == "image_url");
  CHECK(j["content"][1]["image_url"]["url"] == "data:image/png;base64,AAAA");
}

TEST_CASE("an image-only user message omits the empty text part",
          "[chat_request][image]") {
  llmcli::ImagePart img{"p.png", "image/png", "data:image/png;base64,AAAA", 3};
  json j = to_json(Message::user("", {img}));
  REQUIRE(j["content"].is_array());
  REQUIRE(j["content"].size() == 1);
  CHECK(j["content"][0]["type"] == "image_url");
}
