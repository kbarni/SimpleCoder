#include "llm/ChatResponse.hpp"

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using llmcli::StreamParser;
using llmcli::ToolCall;

namespace {

// A single OpenAI-style SSE event line.
std::string sse(const std::string& json) { return "data: " + json + "\n\n"; }

}  // namespace

TEST_CASE("plain content chunks concatenate", "[chat_response]") {
  StreamParser p;
  p.feed(sse(R"({"choices":[{"delta":{"role":"assistant"}}]})"));
  p.feed(sse(R"({"choices":[{"delta":{"content":"Hel"}}]})"));
  p.feed(sse(R"({"choices":[{"delta":{"content":"lo!"}}]})"));
  p.feed(sse(R"({"choices":[{"delta":{},"finish_reason":"stop"}]})"));
  p.feed("data: [DONE]\n\n");

  CHECK(p.content() == "Hello!");
  CHECK(p.finish_reason() == "stop");
  CHECK(p.done());
  CHECK(p.tool_calls().empty());
}

TEST_CASE("reasoning_content is captured separately from content",
          "[chat_response]") {
  std::string reasoned, answered;
  StreamParser p([&](std::string_view s) { answered += s; },
                 [&](std::string_view s) { reasoned += s; });
  p.feed(sse(R"({"choices":[{"delta":{"reasoning_content":"think"}}]})"));
  p.feed(sse(R"({"choices":[{"delta":{"reasoning_content":"ing"}}]})"));
  p.feed(sse(R"({"choices":[{"delta":{"content":"answer"}}]})"));
  p.feed("data: [DONE]\n\n");

  CHECK(p.reasoning() == "thinking");
  CHECK(p.content() == "answer");
  CHECK(reasoned == "thinking");
  CHECK(answered == "answer");
}

TEST_CASE("content callback fires per delta in order", "[chat_response]") {
  std::vector<std::string> pieces;
  StreamParser p([&](std::string_view s) { pieces.emplace_back(s); });
  p.feed(sse(R"({"choices":[{"delta":{"content":"a"}}]})"));
  p.feed(sse(R"({"choices":[{"delta":{"content":"b"}}]})"));

  CHECK(pieces == std::vector<std::string>{"a", "b"});
}

TEST_CASE("fragmented tool call is reassembled with valid JSON args",
          "[chat_response]") {
  StreamParser p;
  // First fragment carries id + name + start of arguments.
  p.feed(sse(
      R"({"choices":[{"delta":{"tool_calls":[{"index":0,"id":"call_1","type":"function","function":{"name":"read_file","arguments":"{\"path\":"}}]}}]})"));
  // Later fragments append only argument text.
  p.feed(sse(
      R"({"choices":[{"delta":{"tool_calls":[{"index":0,"function":{"arguments":"\"a.txt\""}}]}}]})"));
  p.feed(sse(
      R"({"choices":[{"delta":{"tool_calls":[{"index":0,"function":{"arguments":"}"}}]}}]})"));
  p.feed(sse(R"({"choices":[{"delta":{},"finish_reason":"tool_calls"}]})"));
  p.feed("data: [DONE]\n\n");

  auto calls = p.tool_calls();
  REQUIRE(calls.size() == 1);
  CHECK(calls[0].id == "call_1");
  CHECK(calls[0].name == "read_file");
  CHECK(calls[0].arguments == R"({"path":"a.txt"})");
  CHECK(p.finish_reason() == "tool_calls");

  // The reassembled arguments must be parseable JSON.
  auto args = nlohmann::json::parse(calls[0].arguments);
  CHECK(args["path"] == "a.txt");
}

TEST_CASE("multiple parallel tool calls kept in index order",
          "[chat_response]") {
  StreamParser p;
  p.feed(sse(
      R"({"choices":[{"delta":{"tool_calls":[{"index":1,"id":"b","function":{"name":"write_file","arguments":"{}"}}]}}]})"));
  p.feed(sse(
      R"({"choices":[{"delta":{"tool_calls":[{"index":0,"id":"a","function":{"name":"read_file","arguments":"{}"}}]}}]})"));

  auto calls = p.tool_calls();
  REQUIRE(calls.size() == 2);
  CHECK(calls[0].id == "a");  // index 0 first despite arriving second
  CHECK(calls[1].id == "b");
}

TEST_CASE("bytes split across arbitrary boundaries are handled",
          "[chat_response]") {
  StreamParser p;
  const std::string full =
      sse(R"({"choices":[{"delta":{"content":"split"}}]})") + "data: [DONE]\n\n";

  // Feed one byte at a time.
  for (char ch : full) p.feed(std::string_view(&ch, 1));

  CHECK(p.content() == "split");
  CHECK(p.done());
}

TEST_CASE("comments and malformed chunks are ignored", "[chat_response]") {
  StreamParser p;
  p.feed(": keep-alive\n\n");                       // SSE comment
  p.feed("data: {not valid json}\n\n");             // malformed
  p.feed(sse(R"({"choices":[]})"));                 // empty choices
  p.feed(sse(R"({"choices":[{"delta":{"content":"ok"}}]})"));

  CHECK(p.content() == "ok");
  CHECK_FALSE(p.done());
}

TEST_CASE("usage chunk populates token counts", "[chat_response]") {
  StreamParser p;
  p.feed(sse(R"({"choices":[{"delta":{"content":"hi"}}]})"));
  // Final usage-only chunk: empty choices, top-level usage object.
  p.feed(sse(
      R"({"choices":[],"usage":{"prompt_tokens":120,"completion_tokens":30,"total_tokens":150}})"));

  CHECK(p.content() == "hi");          // delta still captured
  CHECK(p.prompt_tokens() == 120);
  CHECK(p.completion_tokens() == 30);
  CHECK(p.total_tokens() == 150);
}

TEST_CASE("token counts default to zero without a usage chunk",
          "[chat_response]") {
  StreamParser p;
  p.feed(sse(R"({"choices":[{"delta":{"content":"hi"}}]})"));
  CHECK(p.total_tokens() == 0);
}
