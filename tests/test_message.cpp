#include "llm/Message.hpp"

#include <catch2/catch_test_macros.hpp>

using llmcli::Message;
using llmcli::Role;
using llmcli::ToolCall;

TEST_CASE("role <-> string round-trips", "[message]") {
  for (Role r : {Role::System, Role::User, Role::Assistant, Role::Tool}) {
    auto back = llmcli::role_from_string(llmcli::to_string(r));
    REQUIRE(back.has_value());
    CHECK(*back == r);
  }
  CHECK_FALSE(llmcli::role_from_string("nonsense").has_value());
  CHECK(llmcli::to_string(Role::Assistant) == "assistant");
}

TEST_CASE("factory helpers set role and content", "[message]") {
  CHECK(Message::system("s").role == Role::System);
  CHECK(Message::user("u").content == "u");

  auto a = Message::assistant("hi");
  CHECK(a.role == Role::Assistant);
  CHECK(a.tool_calls.empty());
  CHECK(a.tool_call_id.empty());
}

TEST_CASE("tool result carries its call id", "[message]") {
  auto m = Message::tool_result("call_42", "result body");
  CHECK(m.role == Role::Tool);
  CHECK(m.tool_call_id == "call_42");
  CHECK(m.content == "result body");
}

TEST_CASE("tool call fields survive without loss", "[message]") {
  ToolCall tc{"call_1", "read_file", R"({"path":"a.txt"})"};

  auto assistant = Message::assistant("");
  assistant.tool_calls.push_back(tc);

  REQUIRE(assistant.tool_calls.size() == 1);
  CHECK(assistant.tool_calls[0] == tc);
  CHECK(assistant.tool_calls[0].id == "call_1");
  CHECK(assistant.tool_calls[0].name == "read_file");
  CHECK(assistant.tool_calls[0].arguments == R"({"path":"a.txt"})");
}

TEST_CASE("message equality compares all fields", "[message]") {
  CHECK(Message::user("x") == Message::user("x"));
  CHECK_FALSE(Message::user("x") == Message::user("y"));
  CHECK_FALSE(Message::user("x") == Message::assistant("x"));
}
